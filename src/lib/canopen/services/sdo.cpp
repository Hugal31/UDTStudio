/**
 ** This file is part of the UDTStudio project.
 ** Copyright 2019-2021 UniSwarm
 **
 ** This program is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 3 of the License, or
 ** (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/

#include "sdo.h"

#include "canopenbus.h"

#include <QDataStream>
#include <QDebug>
#include <QThread>

enum
{
    ATTEMPT_ERROR_MAX = 3,
    TIME_BLOCK_DOWNALOAD = 1,
    TIMEOUT_SDO = 1800
};

SDO::SDO(Node *node)
    : Service(node)
{
    _nodeId = node->nodeId();
    _cobIdClientToServer = 0x600;
    _cobIdServerToClient = 0x580;
    _cobIds.append(_cobIdClientToServer + _nodeId);
    _cobIds.append(_cobIdServerToClient + _nodeId);

    _timeoutTimer = new QTimer(this);
    connect(_timeoutTimer, &QTimer::timeout, this, &SDO::timeout);

    _subBlockDownloadTimer = new QTimer(this);
    connect(_subBlockDownloadTimer, &QTimer::timeout, this, &SDO::sdoBlockDownloadSubBlock);

    _status = SDO_STATE_FREE;
    _requestCurrent = nullptr;
}

SDO::~SDO()
{
    delete _timeoutTimer;
}

QString SDO::type() const
{
    return QLatin1String("SDO");
}

quint32 SDO::cobIdClientToServer() const
{
    return _cobIdClientToServer;
}

quint32 SDO::cobIdServerToClient() const
{
    return _cobIdServerToClient;
}

/**
 * @brief Dispatcher frame
 * @param frame
 */
void SDO::parseFrame(const QCanBusFrame &frame)
{
    if (frame.frameId() == _cobIdClientToServer + _nodeId)
    {
        processingFrameFromClient(frame);
    }
    else if (frame.frameId() == _cobIdServerToClient + _nodeId)
    {
        processingFrameFromServer(frame);
    }
    else
    {
        return;
    }
}

void SDO::reset()
{
    _timeoutTimer->stop();
    qDeleteAll(_requestQueue);
    _requestQueue.clear();
    _status = SDO_STATE_FREE;
}

/**
 * @brief Dispatcher frame SDO from client
 * @param frame
 */
void SDO::processingFrameFromClient(const QCanBusFrame &frame)
{
    Q_UNUSED(frame)
}

/**
 * @brief Dispatcher frame SDO from Server (device)
 * @param frame
 */
void SDO::processingFrameFromServer(const QCanBusFrame &frame)
{
    if (_requestCurrent == nullptr)
    {
        return;
    }

    if (frame.payload().size() != 8)
    {
        sendErrorSdoToDevice(SDOAbortCodes::CO_SDO_ABORT_CODE_GENERAL_ERROR);
        return;
    }
    if (_requestCurrent->state == STATE_BLOCK_UPLOAD)
    {
        sdoBlockUploadSubBlock(frame);
        return;
    }

    quint8 scs = static_cast<quint8>(frame.payload().at(0) & SDO_CSS_MASK);

    _timeoutTimer->stop();
    switch (scs)
    {
        case SCS::SDO_SCS_SERVER_UPLOAD_INITIATE:
            sdoUploadInitiate(frame);
            break;

        case SCS::SDO_SCS_SERVER_UPLOAD_SEGMENT:
            sdoUploadSegment(frame);
            break;

        case SCS::SDO_SCS_SERVER_BLOCK_UPLOAD:
            sdoBlockUpload(frame);
            break;

        case SCS::SDO_SCS_SERVER_DOWNLOAD_INITIATE:
            sdoDownloadInitiate(frame);
            break;

        case SCS::SDO_SCS_SERVER_DOWNLOAD_SEGMENT:
            sdoDownloadSegment(frame);
            break;

        case SCS::SDO_SCS_SERVER_BLOCK_DOWNLOAD:
            sdoBlockDownload(frame);
            break;

        case SCS::SDO_SCS_CLIENT_ABORT:
        {
            quint32 error = arrangeDataUpload(frame.payload().mid(4, 4), QMetaType::Type::UInt).toUInt();
            qDebug() << "ABORT received : Index :" << QString::number(indexFromFrame(frame), 16).toUpper()
                     << ", SubIndex :" << QString::number(subIndexFromFrame(frame), 16).toUpper() << ", abort :" << QString::number(error, 16).toUpper() << sdoAbort(error);

            setErrorToObject(static_cast<SDOAbortCodes>(error));
            break;
        }
        default:
            break;
    }
}

bool SDO::hasRequestPending() const
{
    return (!_requestQueue.isEmpty());
}

/**
 * @brief Status
 * @return status
 */
SDO::Status SDO::status() const
{
    return _status;
}

QString SDO::sdoAbort(quint32 error) const
{
    switch (error)
    {
        case SDO::CO_SDO_ABORT_CODE_BIT_NOT_ALTERNATED:
            return QStringLiteral("Toggle bit not alternated");
        case SDO::CO_SDO_ABORT_CODE_TIMED_OUT:
            return QStringLiteral("SDO protocol timed out");
        case SDO::CO_SDO_ABORT_CODE_CMD_NOT_VALID:
            return QStringLiteral("Client/server command specifier not valid or unknown");
        case SDO::CO_SDO_ABORT_CODE_INVALID_BLOCK_SIZE:
            return QStringLiteral("Invalid block size (block mode only)");
        case SDO::CO_SDO_ABORT_CODE_INVALID_SEQ_NUMBER:
            return QStringLiteral("Invalid sequence number (block mode only)");
        case SDO::CO_SDO_ABORT_CODE_CRC_ERROR:
            return QStringLiteral("CRC error (block mode only)");
        case SDO::CO_SDO_ABORT_CODE_OUT_OF_MEMORY:
            return QStringLiteral("Out of memory");
        case SDO::CO_SDO_ABORT_CODE_UNSUPPORTED_ACCESS:
            return QStringLiteral("Unsupported access to an object");
        case SDO::CO_SDO_ABORT_CODE_WRITE_ONLY:
            return QStringLiteral("Attempt to read a write only object");
        case SDO::CO_SDO_ABORT_CODE_READ_ONLY:
            return QStringLiteral("Attempt to write a read only object");
        case SDO::CO_SDO_ABORT_CODE_NO_OBJECT:
            return QStringLiteral("Object does not exist in the object dictionary");
        case SDO::CO_SDO_ABORT_CODE_NO_SUBINDEX:
            return QStringLiteral("Sub-index does not exist");
        case SDO::CO_SDO_ABORT_CODE_CANNOT_MAP_PDO:
            return QStringLiteral("Object cannot be mapped to the PDO");
        case SDO::CO_SDO_ABORT_CODE_EXCEED_PDO_LENGTH:
            return QStringLiteral("The number and length of the objects to be mapped would exceed PDO length");
        case SDO::CO_SDO_ABORT_CODE_PARAM_IMCOMPATIBILITY:
            return QStringLiteral("General parameter incompatibility reason");
        case SDO::CO_SDO_ABORT_CODE_ITRN_IMCOMPATIBILITY:
            return QStringLiteral("General internal incompatibility in the device");
        case SDO::CO_SDO_ABORT_CODE_FAILED_HARDWARE_ERR:
            return QStringLiteral("Access failed due to an hardware error");
        case SDO::CO_SDO_ABORT_CODE_LENGTH_DOESNT_MATCH:
            return QStringLiteral("Data type does not match, length of service parameter does not match");
        case SDO::CO_SDO_ABORT_CODE_LENGTH_TOO_HIGH:
            return QStringLiteral("Data type does not match, length of service parameter too high");
        case SDO::CO_SDO_ABORT_CODE_LENGTH_TOO_LOW:
            return QStringLiteral("Data type does not match, length of service parameter too low");
        case SDO::CO_SDO_ABORT_CODE_INVALID_VALUE:
            return QStringLiteral("Invalid value for parameter (download only)");
        case SDO::CO_SDO_ABORT_CODE_VALUE_TOO_HIGH:
            return QStringLiteral("Value of parameter written too high (download only)");
        case SDO::CO_SDO_ABORT_CODE_VALUE_TOO_LOW:
            return QStringLiteral("Value of parameter written too low (download only)");
        case SDO::CO_SDO_ABORT_CODE_MAX_VALUE_LESS_MIN:
            return QStringLiteral("Maximum value is less than minimum value");
        case SDO::CO_SDO_ABORT_CODE_RESRC_NOT_AVAILABLE:
            return QStringLiteral("esource not available: SDO connection");
        case SDO::CO_SDO_ABORT_CODE_GENERAL_ERROR:
            return QStringLiteral("General error");
        case SDO::CO_SDO_ABORT_CODE_CANNOT_TRANSFERRED_1:
            return QStringLiteral("Data cannot be transferred or stored to the application");
        case SDO::CO_SDO_ABORT_CODE_CANNOT_TRANSFERRED_2:
            return QStringLiteral("Data cannot be transferred or stored to the application because of local control");
        case SDO::CO_SDO_ABORT_CODE_CANNOT_TRANSFERRED_3:
            return QStringLiteral("Data cannot be transferred or stored to the application because of the present device state");
        case SDO::CO_SDO_ABORT_CODE_NO_OBJECT_DICO:
            return QStringLiteral("Object dictionary dynamic generation fails or no object dictionary is present");
        case SDO::CO_SDO_ABORT_CODE_NO_DATA_AVAILABLE:
            return QStringLiteral("No data available");
        default:
            return QStringLiteral("Unknown error code");
    }
}

/**
 * @brief Taking into account of a new readind request, create request and put in queue
 * @param index
 * @param subIndex
 * @param data
 * @return 0->ok 1->nok
 */
bool SDO::uploadData(quint16 index, quint8 subindex, QMetaType::Type dataType)
{
    bool existing = false;
    for (RequestSdo *req : qAsConst(_requestQueue))
    {
        if (req->index == index && req->subIndex == subindex)
        {
            existing = true;
        }
    }

    if (!existing)
    {
        RequestSdo *request = new RequestSdo();
        request->index = index;
        request->subIndex = subindex;
        request->dataType = dataType;
        request->size = static_cast<quint32>(QMetaType::sizeOf(QMetaType::Type(dataType)));
        request->state = STATE_UPLOAD;
        _requestQueue.enqueue(request);
    }

    nextRequest();
    return true;
}

/**
 * @brief Taking into account of a new writing request, create request and put in queue
 * @param index
 * @param subIndex
 * @param data
 * @return 0->ok 1->nok
 */
bool SDO::downloadData(quint16 index, quint8 subindex, const QVariant &data)
{
    RequestSdo *request = new RequestSdo();
    request->index = index;
    request->subIndex = subindex;

    request->data = data;
    request->dataType = QMetaType::Type(data.type());
    request->state = STATE_DOWNLOAD;

    if (request->dataType != QMetaType::Type::QByteArray)
    {
        QDataStream req(&request->dataByte, QIODevice::WriteOnly);
        req.setByteOrder(QDataStream::LittleEndian);
        arrangeDataDownload(req, data);
        request->size = static_cast<quint32>(QMetaType::sizeOf(QMetaType::Type(data.type())));
    }
    else
    {
        request->dataByte = data.toByteArray();
        request->size = static_cast<quint32>(request->dataByte.size());
    }

    _requestQueue.enqueue(request);
    nextRequest();
    return true;
}

/**
 * @brief Dispatched of SDO Upload protocol (Expedited/Segmented/Block)
 * @return 0->ok 1->nok
 */
bool SDO::uploadDispatcher()
{
    quint8 cmd = 0;
    NodeSubIndex *subIndex = nullptr;
    if (_node->nodeOd()->subIndexExist(_requestCurrent->index, _requestCurrent->subIndex))
    {
        subIndex = _node->nodeOd()->index(_requestCurrent->index)->subIndex(_requestCurrent->subIndex);
    }

    if ((subIndex != nullptr) && (subIndex->dataType() == NodeSubIndex::DDOMAIN))
    {
        cmd = CCS::SDO_CCS_CLIENT_BLOCK_UPLOAD;
        _requestCurrent->blksize = BLOCK_BLOCK_SIZE;
        sendSdoRequest(cmd, _requestCurrent->index, _requestCurrent->subIndex, _requestCurrent->blksize, 0);
        _requestCurrent->state = STATE_UPLOAD;
    }
    else
    {
        cmd = CCS::SDO_CCS_CLIENT_UPLOAD_INITIATE;
        sendSdoRequest(cmd, _requestCurrent->index, _requestCurrent->subIndex);
        _requestCurrent->state = STATE_UPLOAD;
    }

    return true;
}

/**
 * @brief Get index from frame
 * @param frame
 * @return index
 */
quint16 SDO::indexFromFrame(const QCanBusFrame &frame)
{
    quint16 index = 0;
    QByteArray sdoWriteReqPayload = frame.payload().mid(1, 2);
    QDataStream request(&sdoWriteReqPayload, QIODevice::ReadOnly);
    request.setByteOrder(QDataStream::LittleEndian);
    request >> index;
    return index;
}

/**
 * @brief Get sub index from frame
 * @param frame
 * @return index
 */
quint8 SDO::subIndexFromFrame(const QCanBusFrame &frame)
{
    return static_cast<quint8>(frame.payload().at(3));
}

/**
 * @brief Management SDO Initiale Upload protocol
 * @param frame
 * @return 0->ok 1->nok
 */
bool SDO::sdoUploadInitiate(const QCanBusFrame &frame)
{
    quint8 transferType = (static_cast<quint8>(frame.payload().at(0) & Flag::SDO_E_MASK)) >> 1;
    quint8 sizeIndicator = static_cast<quint8>(frame.payload().at(0) & Flag::SDO_S_SIZE_MASK);
    quint8 cmd = 0;
    quint16 index = indexFromFrame(frame);
    quint8 subindex = subIndexFromFrame(frame);

    if ((index != _requestCurrent->index) || (subindex != _requestCurrent->subIndex))
    {
        sendErrorSdoToDevice(CO_SDO_ABORT_CODE_CMD_NOT_VALID);
        return false;
    }

    if (transferType == SDO_E_EXPEDITED)
    {
        if (sizeIndicator == 1)  // data set size is indicated
        {
            _requestCurrent->stay = (4 - (((frame.payload().at(0)) & SDO_N_NUMBER_INIT_MASK) >> 2));
            _requestCurrent->dataByte.append(frame.payload().mid(4, static_cast<quint8>(_requestCurrent->stay)));
        }
        else
        {
            // d contains unspecified number of bytes to be uploaded.
        }
        endRequest();
    }
    else if (transferType == Flag::SDO_E_NORMAL)
    {
        if (sizeIndicator == 0)  // data set size is not indicated
        {
            // NOT USED -> ERROR d is reserved for further use.
        }

        QByteArray sdoWriteReqPayload = frame.payload().mid(4, 4);
        QDataStream request(&sdoWriteReqPayload, QIODevice::ReadOnly);
        request.setByteOrder(QDataStream::LittleEndian);
        request >> _requestCurrent->size;
        _requestCurrent->stay = _requestCurrent->size;

        cmd = CCS::SDO_CCS_CLIENT_UPLOAD_SEGMENT;
        _requestCurrent->toggle = 0;
        cmd |= (_requestCurrent->toggle << 4) & SDO_TOGGLE_MASK;

        sendSdoRequest(cmd);
        _requestCurrent->state = STATE_UPLOAD_SEGMENT;
    }
    else
    {
        // ERROR
        _requestCurrent->state = STATE_FREE;
        sendErrorSdoToDevice(CO_SDO_ABORT_CODE_CMD_NOT_VALID);
        return false;
    }

    return true;
}

/**
 * @brief Management SDO Segmented Upload protocol
 * @param frame
 * @return 0->ok 1->nok
 */
bool SDO::sdoUploadSegment(const QCanBusFrame &frame)
{
    quint8 cmd = 0;
    quint8 size = 0;
    quint8 toggle = static_cast<quint8>((frame.payload().at(0) & SDO_TOGGLE_MASK));

    if (_requestCurrent->state != STATE_UPLOAD_SEGMENT)
    {
        // Wrong State
        qDebug() << ">> SDO::sdoUploadSegment : Wrong State" << frame.payload();
        return false;
    }

    if (toggle != (_requestCurrent->toggle & SDO_TOGGLE_MASK))
    {
        sendErrorSdoToDevice(CO_SDO_ABORT_CODE_BIT_NOT_ALTERNATED);
        return false;
    }

    size = (SDO_SG_SIZE - (((frame.payload().at(0)) & SDO_N_NUMBER_SEG_MASK) >> 1));

    _requestCurrent->dataByte.append(frame.payload().mid(1, size));
    _requestCurrent->stay -= size;

    if ((frame.payload().at(0) & SDO_C_MORE_MASK) == SDO_C_MORE)  // no more segments to be uploaded
    {
        _requestCurrent->state = STATE_UPLOAD;
        endRequest();
    }
    else  // more segments to be uploaded (C=0)
    {
        cmd = CCS::SDO_CCS_CLIENT_UPLOAD_SEGMENT;
        _requestCurrent->toggle = ~_requestCurrent->toggle;
        cmd |= (_requestCurrent->toggle << 4) & SDO_TOGGLE_MASK;

        sendSdoRequest(cmd);
        _requestCurrent->state = STATE_UPLOAD_SEGMENT;
    }

    return true;
}

/**
 * @brief Management SDO Block Upload protocol
 * @param frame
 * @return 0->ok 1->nok
 */
bool SDO::sdoBlockUpload(const QCanBusFrame &frame)
{
    quint8 cmd = 0;
    quint16 index = indexFromFrame(frame);
    quint8 subindex = subIndexFromFrame(frame);

    quint8 ss = static_cast<quint8>(frame.payload().at(0) & SS::SDO_SCS_SERVER_BLOCK_UPLOAD_SS_END_RESP);
    if ((ss == SS::SDO_SCS_SERVER_BLOCK_UPLOAD_SS_INIT_RESP) && (_requestCurrent->state == STATE_UPLOAD))
    {
        if ((index != _requestCurrent->index) || (subindex != _requestCurrent->subIndex))
        {
            sendErrorSdoToDevice(CO_SDO_ABORT_CODE_CMD_NOT_VALID);
            return false;
        }
        if (static_cast<quint8>(frame.payload().at(0) & BLOCK_SIZE) == BLOCK_SIZE)
        {
            QByteArray sdoWriteReqPayload = frame.payload().mid(4, 4);
            QDataStream request(&sdoWriteReqPayload, QIODevice::ReadOnly);
            request.setByteOrder(QDataStream::LittleEndian);
            request >> _requestCurrent->size;
            _requestCurrent->stay = _requestCurrent->size;
        }
        else
        {
            sendErrorSdoToDevice(CO_SDO_ABORT_CODE_LENGTH_DOESNT_MATCH);
            return false;
        }

        cmd = CCS::SDO_CCS_CLIENT_BLOCK_UPLOAD;
        cmd |= SDO_CCS_CLIENT_BLOCK_UPLOAD_CS_START;
        sendSdoRequest(cmd);
        _timeoutTimer->stop();
        _requestCurrent->state = STATE_BLOCK_UPLOAD;
        _requestCurrent->seqno = 1;
        _requestCurrent->dataByteBySegment.clear();
        _requestCurrent->ackseq = 0;
        _requestCurrent->error = false;
    }
    else if ((ss == SS::SDO_SCS_SERVER_BLOCK_UPLOAD_SS_END_RESP) && (_requestCurrent->state == STATE_BLOCK_UPLOAD_END))
    {
        quint8 n = (frame.payload().at(0) & BLOCK_N_NUMBER_MASK) >> 2;
        if (n != _requestCurrent->stay)
        {
            sendErrorSdoToDevice(CO_SDO_ABORT_CODE_INVALID_BLOCK_SIZE);
            return false;
        }
        _requestCurrent->dataByte.remove(_requestCurrent->dataByte.size() - n, n);

        if (static_cast<quint32>(_requestCurrent->dataByte.size()) != _requestCurrent->size)
        {
            sendErrorSdoToDevice(CO_SDO_ABORT_CODE_INVALID_BLOCK_SIZE);
            return false;
        }
        cmd = CCS::SDO_CCS_CLIENT_BLOCK_UPLOAD;
        cmd |= CS::SDO_CCS_CLIENT_BLOCK_UPLOAD_CS_END_REQ;
        _requestCurrent->state = STATE_UPLOAD;
        sendSdoRequest(cmd);
        endRequest();
    }

    return true;
}

/**
 * @brief Dispatched of SDO Download protocol (Expedited/Segmented/Block)
 * @param frame
 * @return 0->ok 1->nok
 */
bool SDO::sdoBlockUploadSubBlock(const QCanBusFrame &frame)
{
    quint8 cmd = 0;
    quint8 moreBlockSegments = 0;
    quint8 reveiveSeqno = 0;

    reveiveSeqno = frame.payload().at(0) & BLOCK_SEQNO_MASK;
    if ((_requestCurrent->seqno != reveiveSeqno) && (!_requestCurrent->error))
    {
        // ERROR SEQUENCE NUMBER
        _requestCurrent->error = true;
        _requestCurrent->ackseq = 0;
        _requestCurrent->dataByteBySegment.clear();
        qDebug() << ">> SDO::sdoBlockUploadSubBlock : Error sequence number detected" << frame.payload();
    }
    else if (!_requestCurrent->error)
    {
        _requestCurrent->ackseq = _requestCurrent->seqno;
        _requestCurrent->dataByteBySegment.append(frame.payload().mid(1, SDO_SG_SIZE));
    }

    moreBlockSegments = (frame.payload().at(0) & BLOCK_C_MORE_SEG);
    if ((_requestCurrent->seqno >= _requestCurrent->blksize) || (moreBlockSegments == BLOCK_C_MORE_SEG))
    {
        if (!_requestCurrent->error)
        {
            _requestCurrent->dataByte.append(_requestCurrent->dataByteBySegment);
            if (moreBlockSegments == BLOCK_C_MORE_SEG)
            {
                // _request->stay -> here, it's a number in excess
                _requestCurrent->stay = static_cast<quint32>(_requestCurrent->dataByteBySegment.size()) - _requestCurrent->stay;
                _requestCurrent->blksize = 0;
                _requestCurrent->state = STATE_BLOCK_UPLOAD_END;
            }
            else
            {
                _requestCurrent->stay -= static_cast<quint32>(_requestCurrent->dataByteBySegment.size());
                _requestCurrent->blksize = calculateBlockSize(_requestCurrent->stay);
            }
        }
        else
        {
            _requestCurrent->error = false;
            _requestCurrent->blksize = calculateBlockSize(_requestCurrent->stay);
        }
        _requestCurrent->dataByteBySegment.clear();
        cmd = CCS::SDO_CCS_CLIENT_BLOCK_UPLOAD;
        cmd |= CS::SDO_CCS_CLIENT_BLOCK_UPLOAD_CS_RESP;
        sendSdoRequest(cmd, _requestCurrent->ackseq, _requestCurrent->blksize);
        _requestCurrent->seqno = 0;
    }
    _requestCurrent->seqno++;

    return true;
}

/**
 * @brief Calculate seg number for the next block, Block download
 */
quint8 SDO::calculateBlockSize(quint32 size)
{
    if (((size + SDO_SG_SIZE) / SDO_SG_SIZE) >= BLOCK_BLOCK_SIZE)
    {
        return BLOCK_BLOCK_SIZE;
    }

    return static_cast<quint8>((_requestCurrent->stay + SDO_SG_SIZE) / SDO_SG_SIZE);
}

/**
 * @brief Dispatched of SDO Download protocol (Expedited/Segmented/Block)
 * @return 0->ok 1->nok
 */
bool SDO::downloadDispatcher()
{
    quint8 cmd = 0;

    NodeSubIndex *subIndex = nullptr;
    if (_node->nodeOd()->subIndexExist(_requestCurrent->index, _requestCurrent->subIndex))
    {
        subIndex = _node->nodeOd()->index(_requestCurrent->index)->subIndex(_requestCurrent->subIndex);
    }

    if ((subIndex != nullptr) && (subIndex->dataType() == NodeSubIndex::DDOMAIN))
    {
        cmd = CCS::SDO_CCS_CLIENT_BLOCK_DOWNLOAD;
        cmd |= FlagBlock::BLOCK_SIZE;

        QByteArray size;
        QDataStream req(&size, QIODevice::WriteOnly);
        req.setByteOrder(QDataStream::LittleEndian);
        arrangeDataDownload(req, QVariant(static_cast<quint32>(_requestCurrent->size)));

        sendSdoRequest(cmd, _requestCurrent->index, _requestCurrent->subIndex, size);

        _requestCurrent->seqno = 1;
        _requestCurrent->stay = _requestCurrent->size;
        _requestCurrent->attemptCount = 0;
    }
    else
    {
        if (_requestCurrent->size <= 4)  // expedited transfer
        {
            cmd = CCS::SDO_CCS_CLIENT_DOWNLOAD_INITIATE;
            cmd |= SDO_E_EXPEDITED << 1;
            cmd |= Flag::SDO_S_SIZE;
            cmd |= ((4 - _requestCurrent->size) << 2) & SDO_N_NUMBER_INIT_MASK;

            sendSdoRequest(cmd, _requestCurrent->index, _requestCurrent->subIndex, _requestCurrent->dataByte);
            _requestCurrent->state = STATE_DOWNLOAD;
        }
        else  // normal transfer
        {
            cmd = CCS::SDO_CCS_CLIENT_DOWNLOAD_INITIATE;
            cmd |= Flag::SDO_S_SIZE;

            QByteArray size;
            QDataStream req(&size, QIODevice::WriteOnly);
            req.setByteOrder(QDataStream::LittleEndian);
            arrangeDataDownload(req, QVariant(static_cast<quint32>(_requestCurrent->size)));

            sendSdoRequest(cmd, _requestCurrent->index, _requestCurrent->subIndex, size);
            _requestCurrent->stay = _requestCurrent->size;
            _requestCurrent->state = STATE_DOWNLOAD_SEGMENT;
        }
    }

    return true;
}

/**
 * @brief Management SDO Initialte Download protocol
 * @param frame
 * @return 0->ok 1->nok
 */
bool SDO::sdoDownloadInitiate(const QCanBusFrame &frame)
{
    quint32 seek = 0;
    QByteArray buffer;
    quint8 cmd = 0;
    quint16 index = indexFromFrame(frame);
    quint8 subindex = subIndexFromFrame(frame);

    if ((index != _requestCurrent->index) || (subindex != _requestCurrent->subIndex))
    {
        sendErrorSdoToDevice(CO_SDO_ABORT_CODE_CMD_NOT_VALID);
        return false;
    }

    if (_requestCurrent->state == STATE_DOWNLOAD_SEGMENT)
    {
        cmd = CCS::SDO_CCS_CLIENT_DOWNLOAD_SEGMENT;
        _requestCurrent->toggle = 0;
        cmd |= _requestCurrent->toggle & SDO_TOGGLE_MASK;
        if (_requestCurrent->size <= SDO_SG_SIZE)
        {
            cmd |= ((SDO_SG_SIZE - _requestCurrent->stay) << 1) & SDO_N_NUMBER_SEG_MASK;
        }

        seek = _requestCurrent->size - _requestCurrent->stay;
        buffer.clear();
        buffer = _requestCurrent->dataByte.mid(static_cast<int32_t>(seek), SDO_SG_SIZE);

        if (_requestCurrent->stay < SDO_SG_SIZE)  // no more segments to be downloaded
        {
            _requestCurrent->state = STATE_DOWNLOAD;
            cmd |= SDO_C_MORE;
            sendSdoRequest(cmd, buffer);
            endRequest();
            return true;
        }

        _requestCurrent->state = STATE_DOWNLOAD_SEGMENT;
        sendSdoRequest(cmd, buffer);
        _requestCurrent->stay -= SDO_SG_SIZE;
    }
    else if (_requestCurrent->state == STATE_DOWNLOAD)
    {
        endRequest();
        return true;
    }

    return true;
}

/**
 * @brief Management SDO Segmented Download protocol
 * @param frame
 * @return 0->ok 1->nok
 */
bool SDO::sdoDownloadSegment(const QCanBusFrame &frame)
{
    quint32 seek = 0;
    QByteArray buffer;
    quint8 cmd = 0;
    quint8 toggle = 0;

    if (_requestCurrent->state != STATE_DOWNLOAD_SEGMENT)
    {
        // Wrong State
        qDebug() << ">> SDO::sdoDownloadSegment : Wrong State" << frame.payload();
        return false;
    }

    toggle = static_cast<quint8>((frame.payload().at(0) & SDO_TOGGLE_MASK));
    if (toggle != _requestCurrent->toggle)
    {
        sendErrorSdoToDevice(CO_SDO_ABORT_CODE_BIT_NOT_ALTERNATED);
        return false;
    }

    cmd = CCS::SDO_CCS_CLIENT_DOWNLOAD_SEGMENT;
    _requestCurrent->toggle = ~_requestCurrent->toggle;
    cmd |= _requestCurrent->toggle & SDO_TOGGLE_MASK;

    seek = _requestCurrent->size - _requestCurrent->stay;
    buffer.clear();
    buffer = _requestCurrent->dataByte.mid(static_cast<int32_t>(seek), SDO_SG_SIZE);

    if (_requestCurrent->stay < SDO_SG_SIZE)
    {
        _requestCurrent->state = STATE_DOWNLOAD;
        cmd |= ((SDO_SG_SIZE - _requestCurrent->stay) << 1) & SDO_N_NUMBER_SEG_MASK;
        cmd |= SDO_C_MORE;  // no more segments to be downloaded
        sendSdoRequest(cmd, buffer);
        endRequest();
        return true;
    }

    _requestCurrent->state = STATE_DOWNLOAD_SEGMENT;
    sendSdoRequest(cmd, buffer);
    _requestCurrent->stay -= SDO_SG_SIZE;

    return true;
}

/**
 * @brief Management SDO Block Download protocol
 * @param frame
 * @return 0->ok 1->nok
 */
bool SDO::sdoBlockDownload(const QCanBusFrame &frame)
{
    quint16 index = 0;
    quint8 subindex = 0;

    quint8 ss = static_cast<quint8>(frame.payload().at(0) & SS::SDO_SCS_SERVER_BLOCK_DOWNLOAD_SS_MASK);

    if (_requestCurrent == nullptr)
    {
        sendErrorSdoToDevice(CO_SDO_ABORT_CODE_CMD_NOT_VALID);
        return false;
    }

    if ((ss == SS::SDO_SCS_SERVER_BLOCK_DOWNLOAD_SS_INIT_RESP) && (_requestCurrent->state == STATE_DOWNLOAD))
    {
        index = indexFromFrame(frame);
        subindex = subIndexFromFrame(frame);
        if (index != _requestCurrent->index || subindex != _requestCurrent->subIndex)
        {
            qDebug() << ">>SDO::sdoBlockDownload, Error index, subindex not corresponding";
            sendErrorSdoToDevice(CO_SDO_ABORT_CODE_CMD_NOT_VALID);
            return false;
        }

        _requestCurrent->blksize = static_cast<quint8>(frame.payload().at(4));
        if (_requestCurrent->blksize > BLOCK_BLOCK_SIZE)
        {
            sendErrorSdoToDevice(CO_SDO_ABORT_CODE_INVALID_BLOCK_SIZE);
            return false;
        }

        _requestCurrent->state = STATE_BLOCK_DOWNLOAD;
        _requestCurrent->seqno = 1;
        _subBlockDownloadTimer->start(TIME_BLOCK_DOWNALOAD);
        _timeoutTimer->stop();
    }
    else if (ss == SS::SDO_SCS_SERVER_BLOCK_DOWNLOAD_SS_RESP)
    {
        _requestCurrent->blksize = static_cast<quint8>(frame.payload().at(2));
        if (_requestCurrent->blksize > BLOCK_BLOCK_SIZE)
        {
            sendErrorSdoToDevice(CO_SDO_ABORT_CODE_INVALID_BLOCK_SIZE);
            return false;
        }

        quint8 ackseq = static_cast<quint8>(frame.payload().at(1));
        if (ackseq > BLOCK_BLOCK_SIZE)
        {
            sendErrorSdoToDevice(CO_SDO_ABORT_CODE_INVALID_SEQ_NUMBER);
            return false;
        }
        if (ackseq != (_requestCurrent->seqno - 1))
        {
            // ERROR sequence detection from server -> Re-Send block
            qDebug() << ">>SDO::sdoBlockDownload, Error sequence detection from server, ackseq : " << ackseq << "attempt:" << _requestCurrent->attemptCount;
            _requestCurrent->stay += (_requestCurrent->seqno - 1) * SDO_SG_SIZE;
            _requestCurrent->state = STATE_BLOCK_DOWNLOAD;
            _requestCurrent->attemptCount++;
            if (_requestCurrent->attemptCount == ATTEMPT_ERROR_MAX)
            {
                _requestCurrent->attemptCount = 0;
                sendErrorSdoToDevice(CO_SDO_ABORT_CODE_INVALID_SEQ_NUMBER);
                return false;
            }
        }

        if (_requestCurrent->state == STATE_BLOCK_DOWNLOAD)
        {
            _requestCurrent->seqno = 1;
            _subBlockDownloadTimer->start(TIME_BLOCK_DOWNALOAD);
            _timeoutTimer->stop();
        }
        else if (_requestCurrent->state == STATE_BLOCK_DOWNLOAD_END)
        {
            sdoBlockDownloadEnd();
        }
    }
    else if (ss == SS::SDO_SCS_SERVER_BLOCK_DOWNLOAD_SS_END_RESP)
    {
        _requestCurrent->state = STATE_DOWNLOAD;
        endRequest();
        return true;
    }
    return true;
}

/**
 * @brief Send a sub block
 */
void SDO::sdoBlockDownloadSubBlock()
{
    quint32 seek = 0;
    QByteArray buffer;

    if (_requestCurrent == nullptr)
    {
        _subBlockDownloadTimer->stop();
        return;
    }

    if (_requestCurrent->seqno <= _requestCurrent->blksize)
    {
        seek = _requestCurrent->size - _requestCurrent->stay;
        buffer.clear();
        buffer = _requestCurrent->dataByte.mid(static_cast<int32_t>(seek), SDO_SG_SIZE);

        sendSdoRequest(true, _requestCurrent->seqno, buffer);
        _requestCurrent->stay -= SDO_SG_SIZE;
        _requestCurrent->seqno++;

        if (_requestCurrent->stay <= SDO_SG_SIZE)
        {
            seek = _requestCurrent->size - _requestCurrent->stay;
            buffer.clear();
            buffer = _requestCurrent->dataByte.mid(static_cast<int32_t>(seek), SDO_SG_SIZE);

            sendSdoRequest(false, _requestCurrent->seqno, buffer);
            _requestCurrent->state = STATE_BLOCK_DOWNLOAD_END;
            _requestCurrent->seqno++;
            _subBlockDownloadTimer->stop();
            return;
        }
    }
    else
    {
        _subBlockDownloadTimer->stop();
        _requestCurrent->state = STATE_BLOCK_DOWNLOAD;
        _timeoutTimer->start(TIMEOUT_SDO);
    }
}

/**
 * @brief Send a last response to device, Protocol Block Donwload
 */
bool SDO::sdoBlockDownloadEnd()
{
    quint8 cmd = 0;
    cmd = CCS::SDO_CCS_CLIENT_BLOCK_DOWNLOAD;
    cmd |= CS::SDO_CCS_CLIENT_BLOCK_DOWNLOAD_CS_END_REQ;
    cmd |= (SDO_SG_SIZE - _requestCurrent->stay) << 2;
    quint16 crc = 0;
    return sendSdoRequest(cmd, crc);
}

/**
 * @brief Send error sdo to device
 * @param error SDO/CIA
 * @return bool value successful or not
 */
void SDO::sendErrorSdoToDevice(SDOAbortCodes error)
{
    sendSdoRequest(CCS::SDO_CCS_CLIENT_ABORT, _requestCurrent->index, _requestCurrent->subIndex, error);
    setErrorToObject(error);
}

/**
 * @brief Set error in object
 * @param error SDO/CIA
 * @return bool value successful or not
 */
void SDO::setErrorToObject(SDOAbortCodes error)
{
    uint8_t flags = NodeOd::FlagsRequest::Error;
    if (_requestCurrent->state == STATE_UPLOAD)
    {
        flags += NodeOd::FlagsRequest::Read;
    }
    else if (_requestCurrent->state == STATE_DOWNLOAD)
    {
        flags += NodeOd::FlagsRequest::Write;
    }

    _node->nodeOd()->updateObjectFromDevice(_requestCurrent->index, _requestCurrent->subIndex, QVariant(error), static_cast<NodeOd::FlagsRequest>(flags));

    _status = SDO_STATE_FREE;
    _requestCurrent->state = STATE_FREE;
    _timeoutTimer->stop();
    nextRequest();
}

/**
 * @brief Management request finished
 */
void SDO::endRequest()
{
    if (_requestCurrent->state == STATE_UPLOAD)
    {
        _node->nodeOd()->updateObjectFromDevice(
            _requestCurrent->index, _requestCurrent->subIndex, arrangeDataUpload(_requestCurrent->dataByte, _requestCurrent->dataType), NodeOd::FlagsRequest::Read);
    }
    else if (_requestCurrent->state == STATE_DOWNLOAD)
    {
        _node->nodeOd()->updateObjectFromDevice(_requestCurrent->index, _requestCurrent->subIndex, _requestCurrent->data, NodeOd::FlagsRequest::Write);
    }

    _status = SDO_STATE_FREE;
    _timeoutTimer->stop();
    nextRequest();
}

/**
 * @brief Management Queue of request
 */
void SDO::nextRequest()
{
    if (_status != SDO_STATE_FREE)
    {
        return;
    }

    if (!_requestQueue.isEmpty())
    {
        _requestCurrent = _requestQueue.dequeue();
        if (_requestCurrent->state == STATE_UPLOAD)
        {
            _status = SDO_STATE_NOT_FREE;
            uploadDispatcher();
        }
        else if (_requestCurrent->state == STATE_DOWNLOAD)
        {
            _status = SDO_STATE_NOT_FREE;
            downloadDispatcher();
        }
    }
    else
    {
        _requestCurrent = nullptr;
    }
}

/**
 * @brief Management timeout, send SDO TIMEOUT on device
 */
void SDO::timeout()
{
    uint32_t error = CO_SDO_ABORT_CODE_TIMED_OUT;
    sendSdoRequest(CCS::SDO_CCS_CLIENT_ABORT, _requestCurrent->index, _requestCurrent->subIndex, error);
    setErrorToObject(static_cast<SDOAbortCodes>(error));
}

/**
 * @brief Management SDO upload initiate
 * @param cmd
 * @param index
 * @param subindex
 * @return bool value successful or not
 */
bool SDO::sendSdoRequest(quint8 cmd, quint16 index, quint8 subindex)
{
    if (!bus()->canWrite())
    {
        return false;
    }

    QByteArray sdoWriteReqPayload;
    QDataStream request(&sdoWriteReqPayload, QIODevice::WriteOnly);
    request.setByteOrder(QDataStream::LittleEndian);
    request << cmd;
    request << index;
    request << subindex;

    // the frame must be a size of 8
    for (int i = sdoWriteReqPayload.size(); i < 8; i++)
    {
        request << static_cast<quint8>(0);
    }

    QCanBusFrame frame;
    frame.setFrameId(_cobIdClientToServer + _nodeId);
    frame.setPayload(sdoWriteReqPayload);

    _timeoutTimer->start(TIMEOUT_SDO);
    return bus()->writeFrame(frame);
}

/**
 * @brief Management SDO upload segment, SDO block upload initiate, SDO block upload ends
 * @param cmd
 * @return bool value successful or not
 */
bool SDO::sendSdoRequest(quint8 cmd)
{
    if (!bus()->canWrite())
    {
        return false;
    }

    QByteArray sdoWriteReqPayload;
    QDataStream request(&sdoWriteReqPayload, QIODevice::WriteOnly);
    request.setByteOrder(QDataStream::LittleEndian);
    request << cmd;

    // the frame must be a size of 8
    for (int i = sdoWriteReqPayload.size(); i < 8; i++)
    {
        request << static_cast<quint8>(0);
    }

    QCanBusFrame frame;
    frame.setFrameId(_cobIdClientToServer + _nodeId);
    frame.setPayload(sdoWriteReqPayload);

    _timeoutTimer->start(TIMEOUT_SDO);
    return bus()->writeFrame(frame);
}

/**
 * @brief Management SDO download initiate
 * @param cmd
 * @param index
 * @param subindex
 * @param data
 * @return bool value successful or not
 */
bool SDO::sendSdoRequest(quint8 cmd, quint16 index, quint8 subindex, const QByteArray &data)
{
    if (!bus()->canWrite())
    {
        return false;
    }

    QByteArray sdoWriteReqPayload;
    QDataStream request(&sdoWriteReqPayload, QIODevice::WriteOnly);
    request.setByteOrder(QDataStream::LittleEndian);
    request << cmd;
    request << index;
    request << subindex;
    request.writeRawData(data.data(), data.size());

    // the frame must be a size of 8
    for (int i = sdoWriteReqPayload.size(); i < 8; i++)
    {
        request << static_cast<quint8>(0);
    }

    QCanBusFrame frame;
    frame.setFrameId(_cobIdClientToServer + _nodeId);
    frame.setPayload(sdoWriteReqPayload);

    _timeoutTimer->start(TIMEOUT_SDO);
    return bus()->writeFrame(frame);
}

/**
 * @brief Management SDO download segment
 * @param cmd
 * @param data
 * @return bool value successful or not
 */
bool SDO::sendSdoRequest(quint8 cmd, const QByteArray &data)
{
    if (!bus()->canWrite())
    {
        return false;
    }

    QByteArray sdoWriteReqPayload;
    QDataStream request(&sdoWriteReqPayload, QIODevice::WriteOnly);
    request << cmd;
    request.writeRawData(data.data(), data.size());

    for (int i = sdoWriteReqPayload.size(); i < 8; i++)
    {
        request << static_cast<quint8>(0);
    }

    QCanBusFrame frame;
    frame.setFrameId(_cobIdClientToServer + _nodeId);
    frame.setPayload(sdoWriteReqPayload);
    _timeoutTimer->start(TIMEOUT_SDO);
    return bus()->writeFrame(frame);
}

/**
 * @brief Management SDO block download end
 * @param cmd
 * @param crc NOT USED = 0
 * @return bool value successful or not
 */
bool SDO::sendSdoRequest(quint8 cmd, quint16 &crc)
{
    if (!bus()->canWrite())
    {
        return false;
    }

    QByteArray sdoWriteReqPayload;
    QDataStream request(&sdoWriteReqPayload, QIODevice::WriteOnly);
    request.setByteOrder(QDataStream::LittleEndian);
    request << cmd;
    request << crc;

    // the frame must be a size of 8
    for (int i = sdoWriteReqPayload.size(); i < 8; i++)
    {
        request << static_cast<quint8>(0);
    }

    QCanBusFrame frame;
    frame.setFrameId(_cobIdClientToServer + _nodeId);
    frame.setPayload(sdoWriteReqPayload);

    _timeoutTimer->start(TIMEOUT_SDO);
    return bus()->writeFrame(frame);
}

/**
 * @brief Management SDO block upload initiate
 * @param cmd
 * @param index
 * @param subindex
 * @param blksize Number of segments per block
 * @param pst protocol switch threshold in bytes to change the SDO transfer protocol NOT USED
 * @return bool value successful or not
 */
bool SDO::sendSdoRequest(quint8 cmd, quint16 index, quint8 subindex, quint8 blksize, quint8 pst)
{
    if (!bus()->canWrite())
    {
        return false;
    }

    QByteArray sdoWriteReqPayload;
    QDataStream request(&sdoWriteReqPayload, QIODevice::WriteOnly);
    request.setByteOrder(QDataStream::LittleEndian);
    request << cmd;
    request << index;
    request << subindex;
    request << blksize;
    request << pst;

    // the frame must be a size of 8
    for (int i = sdoWriteReqPayload.size(); i < 8; i++)
    {
        request << static_cast<quint8>(0);
    }

    QCanBusFrame frame;
    frame.setFrameId(_cobIdClientToServer + _nodeId);
    frame.setPayload(sdoWriteReqPayload);

    _timeoutTimer->start(TIMEOUT_SDO);
    return bus()->writeFrame(frame);
}

/**
 * @brief Management SDO block upload sub-block
 * @param cmd
 * @param ackseq sequence number of last segment that was received successfully during the last block upload
 * @param blksize size of following block
 * @bool return value successful or not
 */
bool SDO::sendSdoRequest(quint8 cmd, quint8 &ackseq, quint8 blksize)
{
    if (!bus()->canWrite())
    {
        return false;
    }

    QByteArray sdoWriteReqPayload;
    QDataStream request(&sdoWriteReqPayload, QIODevice::WriteOnly);
    request.setByteOrder(QDataStream::LittleEndian);
    request << cmd;
    request << ackseq;
    request << blksize;

    // the frame must be a size of 8
    for (int i = sdoWriteReqPayload.size(); i < 8; i++)
    {
        request << static_cast<quint8>(0);
    }

    QCanBusFrame frame;
    frame.setFrameId(_cobIdClientToServer + _nodeId);
    frame.setPayload(sdoWriteReqPayload);

    return bus()->writeFrame(frame);
}

/**
 * @brief Management SDO block download sub-block
 * @param moreSegments indicates whether there are still more segments to be downloaded.
 * @param seqno number of sequence
 * @param segData sequence data
 * @bool return value successful or not
 */
bool SDO::sendSdoRequest(bool moreSegments, quint8 seqno, const QByteArray &segData)
{
    if (!bus()->canWrite())
    {
        return false;
    }

    QByteArray sdoWriteReqPayload;
    QDataStream request(&sdoWriteReqPayload, QIODevice::WriteOnly);
    QCanBusFrame frame;

    if (!moreSegments)
    {
        _timeoutTimer->start(TIMEOUT_SDO);
        seqno |= 0x80;
        request << static_cast<quint8>(seqno);
    }
    else
    {
        request << static_cast<quint8>(seqno);
    }
    request.writeRawData(segData.data(), segData.size());

    // the frame must be a size of 8
    for (int i = sdoWriteReqPayload.size(); i < 8; i++)
    {
        sdoWriteReqPayload.append(static_cast<char>(0));
    }
    frame.setFrameId(_cobIdClientToServer + _nodeId);
    frame.setPayload(sdoWriteReqPayload);

    return bus()->writeFrame(frame);
}

/**
 * @brief SDO abort transfer
 * @param cmd
 * @param index
 * @param subindex
 * @param error SDO/CIA on uint32
 * @bool return value successful or not
 */
bool SDO::sendSdoRequest(quint8 cmd, quint16 index, quint8 subindex, quint32 error)
{
    if (!bus()->canWrite())
    {
        return false;
    }

    QByteArray sdoWriteReqPayload;
    QDataStream request(&sdoWriteReqPayload, QIODevice::WriteOnly);
    request.setByteOrder(QDataStream::LittleEndian);
    request << cmd;
    request << index;
    request << subindex;
    request << error;

    QCanBusFrame frame;
    frame.setFrameId(_cobIdClientToServer + _nodeId);
    frame.setPayload(sdoWriteReqPayload);

    _timeoutTimer->start(TIMEOUT_SDO);
    return bus()->writeFrame(frame);
}

/**
 * @brief Moved a QByteArray received from device to QVariant with type for studio
 * @param data
 * @param type of data
 * @return QVariant arranged value
 */
QVariant SDO::arrangeDataUpload(QByteArray data, QMetaType::Type type)
{
    QDataStream dataStream(&data, QIODevice::ReadOnly);
    dataStream.setByteOrder(QDataStream::LittleEndian);

    switch (type)
    {
        case QMetaType::Int:
            int a;
            dataStream >> a;
            return QVariant(a);

        case QMetaType::UInt:
            unsigned int b;
            dataStream >> b;
            return QVariant(b);

        case QMetaType::LongLong:
            qint64 c;
            dataStream >> c;
            return QVariant(c);

        case QMetaType::ULongLong:
            quint64 d;
            dataStream >> d;
            return QVariant(d);

        case QMetaType::Double:
            double e;
            dataStream >> e;
            return QVariant(e);

        case QMetaType::Long:
        {
            long f;
            memcpy(&f, data.constData(), sizeof(long));
            QVariant w;
            w.setValue(f);
            return w;
        }

        case QMetaType::Short:
            short g;
            dataStream >> g;
            return QVariant(g);

        case QMetaType::Char:
            return QVariant(data);

        case QMetaType::ULong:
        {
            unsigned long i;
            memcpy(&i, data.constData(), sizeof(unsigned long));
            QVariant y;
            y.setValue(i);
            return y;
        }

        case QMetaType::UShort:
        {
            unsigned short j;
            memcpy(&j, data.constData(), sizeof(unsigned short));
            QVariant z;
            z.setValue(j);
            return z;
        }

        case QMetaType::UChar:
        {
            unsigned char k;
            memcpy(&k, data.constData(), sizeof(unsigned char));
            QVariant n;
            n.setValue(k);
            return n;
        }

        case QMetaType::Float:
        {
            float f;
            f = *(const float *)(data.constData());
            return QVariant(f);
        }

        case QMetaType::SChar:
        {
            signed char m;
            memcpy(&m, data.constData(), sizeof(signed char));
            QVariant x;
            x.setValue(m);
            return x;
        }

        case QMetaType::QString:
            return QVariant(QString(data));

        case QMetaType::QByteArray:
            return QVariant(data);

        default:
            break;
    }

    return QVariant();
}

/**
 * @brief Moved a QVariant from studio to QDataStream with a correct typage
 * @param data
 * @return QDataStream arranged value
 */
void SDO::arrangeDataDownload(QDataStream &request, const QVariant &data)
{
    switch (QMetaType::Type(data.type()))
    {
        case QMetaType::Long:
            request << data;
            break;

        case QMetaType::LongLong:
            request << data.value<qint64>();
            break;

        case QMetaType::Int:
            request << data.value<int>();
            break;

        case QMetaType::ULong:
            request << data;
            break;

        case QMetaType::ULongLong:
            request << data.value<quint64>();
            break;

        case QMetaType::UInt:
            request << data.value<unsigned int>();
            break;

        case QMetaType::Double:
            request << data.value<double>();
            break;

        case QMetaType::Short:
            request << data.value<short>();
            break;

        case QMetaType::Char:
            request << data.value<char>();
            break;

        case QMetaType::UShort:
            request << data.value<unsigned short>();
            break;

        case QMetaType::UChar:
            request << data.value<unsigned char>();
            break;

        case QMetaType::Float:
        {
            float f = data.toFloat();
            request.writeRawData((char *)&f, 4);
        }
        break;

        case QMetaType::SChar:
            request << data.value<signed char>();
            break;

        case QMetaType::QString:
            request << data;
            break;

        case QMetaType::QByteArray:
            request << data;
            break;

        default:
            break;
    }
}
