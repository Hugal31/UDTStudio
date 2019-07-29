/**
 ** This file is part of the UDTStudio project.
 ** Copyright 2019 UniSwarm sebastien.caux@uniswarm.eu
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

#include <QFile>
#include <QMap>
#include <QList>
#include <QRegularExpression>
#include <QDebug>
#include <QDateTime>

#include "cgenerator.h"

/**
 * @brief default constructor
 */
CGenerator::CGenerator()
{
}

CGenerator::~CGenerator()
{
}

/**
 * @brief generate OD.h and OD.c files
 * @param object dictionary
 * @param output directory path
 */
bool CGenerator::generate(DeviceConfiguration *od, const QString &filePath) const
{
    generateH(od, filePath);
    generateC(od, filePath);

    return true;
}

bool CGenerator::generate(DeviceDescription *od, const QString &filePath) const
{
    return false;
}

void CGenerator::generate(DeviceDescription *od, const QString &filePath, uint8_t nodeId) const
{
    DeviceConfiguration *deviceConfiguration = DeviceConfiguration::fromDeviceDescription(od, nodeId);
    generate(deviceConfiguration, filePath);
}

/**
 * @brief Generate OD.h file
 * @param object dictionary
 */
void CGenerator::generateH(DeviceConfiguration *od, const QString &filePath) const
{
    QFile hFile(filePath);

    if (!hFile.open(QIODevice::WriteOnly))
        return;

    QTextStream out(&hFile);

    out << "/**\n";
    out << " * Generated .h file\n";

    QString date = QDateTime().currentDateTime().toString("dd-MM-yyyy");
    QString time = QDateTime().currentDateTime().toString("hh:mm AP");
    out << " * Creation date: " << date << "\n";
    out << " * Creation time: " << time << "\n";

    out << " */\n";
    out << "\n";
    out << "#ifndef OD_DATA_H" << "\n";
    out << "#define OD_DATA_H" << "\n";
    out << "\n";
    out << "#include \"od.h\"" << "\n";
    out << "\n";
    out << "// == Number of entries in object dictionary ==" << "\n";
    out << "#define OD_NB_ELEMENTS " << od->indexCount() << "\n";
    out << "\n";
    out << "// ===== struct definitions for records =======" << "\n";

    QMap<uint16_t, Index*> indexes = od->indexes();

    foreach (Index *index, indexes)
    {
        writeRecordDefinitionH(index, out);
    }

    out << "// === struct definitions for memory types ===" << "\n";

    //TODO test read access to know wich memory to use (FLASH/RAM)
    out << "struct sOD_RAM" << "\n";
    out << "{" << "\n";

    foreach (Index *index, indexes)
    {
        writeIndexH(index, out);
    }

    out << "};" << "\n";
    out << "\n";

    //TODO declararion of FLASH memory
    out << "// extern declaration for RAM and FLASH struct" << "\n";
    out << "extern const struct sOD_FLASH OD_FLASH;" << "\n";
    out << "\n";
    out << "// ======== extern declaration of OD ========" << "\n";
    out << "extern const OD_entry_t OD[OD_NB_ELEMENTS];" << "\n";
    out << "extern struct sOD_RAM OD_RAM;" << "\n";
    out << "\n";

    foreach (Index *index, indexes)
    {
        writeDefineH(index, out);
    }

    out << "// ============== function ==================" << "\n";
    out << "void od_initCommIndexes(void);" << "\n";
    out << "void od_initAppIndexes(void);" << "\n";
    out << "\n";
    out << "#endif // OD_DATA_H";
    out << "\n";

    hFile.close();
}

/**
 * @brief Generate OD.c file
 * @param object dictionary
 */
void CGenerator::generateC(DeviceConfiguration *od, const QString &filePath) const
{
    QFile cFile(filePath);

    if (!cFile.open(QIODevice::WriteOnly))
        return;

    QTextStream out(&cFile);

    out << "/**\n";
    out << " * Generated .c file\n";

    QString date = QDateTime().currentDateTime().toString("dd-MM-yyyy");
    QString time = QDateTime().currentDateTime().toString("hh:mm AP");
    out << " * Creation date: " << date << "\n";
    out << " * Creation time: " << time << "\n";

    out << " */\n";
    out << "\n";
    out << "#include \"od_data.h\"" << "\n";
    out << "\n";
    out << "// ==================== initialization =====================" << "\n";
    out << "struct sOD_RAM OD_RAM;" << "\n";
    out << "\n";

    QMap<uint16_t, Index *> indexes = od->indexes();

    foreach (Index *index, indexes)
    {
        if (index->maxSubIndex() > 0)
        {
            foreach (SubIndex* subIndex, index->subIndexes())
            {
                writeCharLineC(subIndex, out);
            }
            continue;
        }

        if (index->subIndexExist(0))
            writeCharLineC(index->subIndex(0), out);
    }

    QList<Index *> appIndexes;
    QList<Index *> commIndexes;

    foreach (Index *index, indexes)
    {
        if (index->index() < 0x1000);

        else if (index->index() < 0x2000)
            commIndexes.append(index);

        else
            appIndexes.append(index);
    }

    out << "void od_initCommIndexes()" << "\n";
    out << "{";
    writeInitRamC(commIndexes, out);
    out << "}" << "\n";
    out << "\n";

    out << "void od_initAppIndexes()" << "\n";
    out << "{";
    writeInitRamC(appIndexes, out);
    out << "}" << "\n";
    out << "\n";

    //TODO initialize struct for FLASH memory
    out << "// ==================== record completion =================" << "\n";

    foreach (Index *index, indexes)
    {
        writeRecordCompletionC(index, out);
    }

    out << "// ============ object dicitonary completion ==============" << "\n";
    out << "const OD_entry_t OD[OD_NB_ELEMENTS] = " << "\n";
    out << "{" << "\n";

    foreach (Index *index, indexes)
    {
        writeOdCompletionC(index, out);
    }

    out << "};";
    out << "\n";

    cFile.close();
}

/**
 * @brief converts a data type to a string
 * @param data type
 * @return data type to C format
 */
QString CGenerator::typeToString(const uint16_t &type) const
{
    switch(type)
    {
    case SubIndex::Type::INTEGER8:
       return QLatin1String("int8_t");

    case SubIndex::Type::INTEGER16:
        return QLatin1String("int16_t");

    case SubIndex::Type::INTEGER32:
        return QLatin1String("int32_t");

    case SubIndex::Type::INTEGER64:
        return QLatin1String("int64_t");

    case SubIndex::Type::BOOLEAN:
    case SubIndex::Type::UNSIGNED8:
       return QLatin1String("uint8_t");

    case SubIndex::Type::UNSIGNED16:
        return QLatin1String("uint16_t");

    case SubIndex::Type::UNSIGNED32:
        return QLatin1String("uint32_t");

    case SubIndex::Type::UNSIGNED64:
        return QLatin1String("uint64_t");

    case SubIndex::Type::REAL32:
        return QLatin1String("float32_t");

    case SubIndex::Type::REAL64:
        return QLatin1String("float64_t");

    case SubIndex::Type::VISIBLE_STRING:
        return QLatin1String("vstring_t");

    case SubIndex::Type::OCTET_STRING:
        return QLatin1String("ostring_t");

    default:
        return QLatin1String("");
    }
}

/**
 * @brief modifies variable name
 * @param variable name
 * @return variable name to C format
 */
QString CGenerator::varNameToString(const QString &name) const
{
    QString modified;
    modified = name.toLower();

    for (int i=0; i < modified.count(); i++)
    {
        if (modified[i] == QChar('-'))
            modified[i] = QChar(' ');

        if (modified[i] == QChar(' '))
            modified[i+1] = modified[i+1].toUpper();
    }

    modified = modified.remove(QChar(' '));

    return modified;
}

/**
 * @brief modifies structure name
 * @param structure name
 * @return structure name to C format
 */
QString CGenerator::structNameToString(const QString &name) const
{
    QString modified;
    QString end("_t");
    modified = name.toLower();
    modified = modified.replace(QChar(' '), QChar('_'));
    modified = modified.append(end);
    return modified;
}

/**
 * @brief converts a sub-index's data
 * @param sub-index which contains data
 * @return data to C hexadecimal format
 */
QString CGenerator::dataToString(const SubIndex *subIndex) const
{
    QString data;

    switch (subIndex->dataType())
    {
    case SubIndex::Type::OCTET_STRING:
    case SubIndex::Type::VISIBLE_STRING:
        data = stringNameToString(subIndex);
        break;

    default:
        data = subIndex->value().toString();
    }

    return data;
}

/**
 * @brief converts data type and object type
 * @param sub-index
 * @return a C hexadecimal value coded on 16 bits
 */
QString CGenerator::typeObjectToString(Index *index, uint8_t subIndex, bool isInRecord = false) const
{
    QString typeObject;

    if (isInRecord)
        typeObject = "0x7";

    else
        typeObject = "0x" + QString::number(index->objectType());

    if (index->subIndex(subIndex)->dataType() <= 0x000F)
        typeObject += "00";

    else if (index->subIndex(subIndex)->dataType() <= 0x00FF)
        typeObject += "0";

    typeObject += QString::number(index->subIndex(subIndex)->dataType(), 16).toUpper();

    return typeObject;
}

/**
 * @brief convert a string name
 * @param sub-index
 * @param sub-index number for arrays
 * @return string name for C file
 */
QString CGenerator::stringNameToString(const SubIndex *subIndex) const
{
    QString string;

    if (subIndex->subIndex() == 0)
        string = varNameToString(subIndex->name()) + "Str";
    else
        string = varNameToString(subIndex->name()) + "Str" + QString::number(subIndex->subIndex());

    return string;
}

/**
 * @brief writes a record's structure in a .h file
 * @param record
 * @param .h file
 */
void CGenerator::writeRecordDefinitionH(Index *index, QTextStream &hFile) const
{
    if ( index->objectType() == Index::Object::RECORD)
    {
        hFile << "typedef struct" << "  // 0x" << QString::number(index->index(), 16) << "\n{\n";

        foreach (const SubIndex *subIndex, index->subIndexes())
        {
            hFile << "    " << typeToString(subIndex->dataType()) << " "<< varNameToString(subIndex->name()) << ";" << "\n";
        }
        hFile << "} " << structNameToString(index->name()) << ";\n\n";
    }
}

/**
 * @brief writes an index definition in a .h file
 * @param index
 * @param .h file
 */
void CGenerator::writeIndexH(Index *index, QTextStream &hFile) const
{
    switch(index->objectType())
    {
    case Index::Object::VAR:
        if (index->subIndexExist(0))
        {
            hFile << "    " << typeToString(index->subIndex(0)->dataType()) << " " << varNameToString(index->name()) << ";";
        }
        break;

    case Index::Object::RECORD:
        hFile << "    " << structNameToString(index->name()) << " " << varNameToString(index->name()) << ";";
        break;

    case Index::Object::ARRAY:
        if (index->subIndexExist(1))
        {
            hFile << "    " << typeToString(index->subIndex(1)->dataType()) << " " << varNameToString(index->name());
            hFile << "[" << index->maxSubIndex()-1 << "]" << ";";
        }
        break;
    }

    hFile << "  // 0x" << QString::number(index->index(), 16) << "\n";
}

/**
 * @brief writes an index initialization in RAM in a .c file
 * @param index
 * @param .c file
 */
void CGenerator::writeRamLineC(Index *index, QTextStream &cFile) const
{
    QMap<uint8_t, SubIndex *> subIndexes;

    switch(index->objectType())
    {
    case Index::Object::VAR:
        if (!index->subIndexExist(0))
            break;

        cFile << "    " << "OD_RAM." << varNameToString(index->name());
        cFile << " = ";
        cFile << dataToString(index->subIndex(0));
        cFile << ";";
        cFile << "  // 0x" << QString::number(index->index(), 16);
        cFile << "\n";
        break;

    case Index::Object::RECORD:
        subIndexes = index->subIndexes();
        foreach (SubIndex *subIndex, subIndexes)
        {
            cFile << "    " << "OD_RAM." << varNameToString(index->name()) << "." << varNameToString(subIndex->name());
            cFile << " = ";
            cFile << dataToString(subIndex);
            cFile << ";";
            cFile << "  // 0x" << QString::number(index->index(), 16) << "." << subIndex->subIndex();
            cFile << "\n";
        }
        break;

    case Index::Object::ARRAY:
        for (uint8_t i = 1; i<index->subIndexesCount(); i++)
        {
            if (!index->subIndexExist(i))
                continue;

            cFile << "    " << "OD_RAM." << varNameToString(index->name()) << "[" << i - 1 << "]";
            cFile << " = ";
            cFile << dataToString(index->subIndex(i));
            cFile << ";";
            cFile << "  // 0x" << QString::number(index->index(), 16) << "." << i-1;
            cFile << "\n";
        }
        break;
    }
}

/**
 * @brief writes a record initialization in ram in a .c file
 * @param index
 * @param .c file
 */
void CGenerator::writeRecordCompletionC(Index *index, QTextStream &cFile) const
{
    if ( index->objectType() == Index::Object::RECORD)
    {
        cFile << "const OD_entrySubIndex_t OD_Record" << QString::number(index->index(), 16).toUpper() << "[" << index->maxSubIndex() << "] =\n";
        cFile << "{\n";

        foreach (SubIndex *subIndex, index->subIndexes())
        {
            cFile << "    " << "{(void*)&OD_RAM." << varNameToString(index->name());
            cFile << "." << varNameToString(subIndex->name());
            //TODO PDOmapping
            cFile << ", " << subIndex->length() << ", " << typeObjectToString(index, subIndex->subIndex(), true) << ", ";
            cFile << "0x" << QString::number(subIndex->accessType(), 16).toUpper() << ", " << subIndex->subIndex();
            cFile << "}," << "\n";
        }

        cFile << "};\n\n";
    }
}

/**
 * @brief writes an object dictionary entry initialisation in a .c file
 * @param index
 * @param .c file
 */
void CGenerator::writeOdCompletionC(Index *index, QTextStream &cFile) const
{
    cFile << "    " << "{";
    //TODO PDOmapping
    cFile << "0x" << QString::number(index->index(), 16).toUpper() << ", " << "0x";

    switch (index->objectType())
    {
    case Index::Object::VAR:
        cFile << "0";
        break;

    case Index::Object::RECORD:
        cFile << index->maxSubIndex()-1;
        break;

    case Index::Object::ARRAY:
        cFile << index->maxSubIndex()-1;
        break;
    }

    cFile << ", ";

    switch (index->objectType())
    {
    case Index::Object::VAR:
        if (!index->subIndexExist(0))
            break;

        cFile << "(void*)&OD_RAM." << varNameToString(index->name()) << ", ";
        cFile << index->subIndex(0)->length() <<  ", " << typeObjectToString(index, 0) << ", " << "0x" << QString::number(index->subIndex(0)->accessType(), 16).toUpper();
        break;

    case Index::Object::RECORD:
        cFile << "(void*)OD_Record" << QString::number(index->index(), 16).toUpper() << ", ";
        cFile << "0" <<  ", " << "0x9000" << ", " << "0x0";
        break;

    case Index::Object::ARRAY:
        if (!index->subIndexExist(1))
            break;

        cFile << "(void*)OD_RAM." << varNameToString(index->name()) << ", ";
        cFile << index->subIndex(1)->length() <<  ", " << typeObjectToString(index, 1) << ", " << "0x" << QString::number(index->subIndex(1)->accessType(), 16).toUpper();
        break;
    }

    cFile << "},";
    cFile << "\n";
}

/**
 * @brief write char[] initialization in a C file
 * @param sub-index
 * @param C file
 * @param sub-index-number for arrays
 */
void CGenerator::writeCharLineC(SubIndex *subIndex, QTextStream &cFile) const
{
    switch (subIndex->dataType())
    {
    case SubIndex::Type::VISIBLE_STRING:
    case SubIndex::Type::OCTET_STRING:
        cFile << "const char " << stringNameToString(subIndex) << "[]" << " = " << "\"" << subIndex->value().toString() << "\"" << ";\n";
        break;
    }
}

void CGenerator::writeInitRamC(QList<Index *> indexes, QTextStream &cFile) const
{
    uint8_t lastOT = 0;
    foreach (Index *index, indexes)
    {
        if (index->objectType() != lastOT
                || index->objectType() == Index::Object::RECORD
                || index->objectType() == Index::Object::ARRAY)
        cFile << "\n";
        writeRamLineC(index, cFile);
        lastOT = index->objectType();
    }
}

void CGenerator::writeDefineH(Index *index, QTextStream &hFile) const
{
    hFile << "#define " << "OD_" << varNameToString(index->name()).toUpper() << " OD_RAM." << varNameToString(index->name()) << "\n";
    hFile << "#define " << "OD_INDEX" << QString::number(index->index(), 16).toUpper() << " OD_RAM." << varNameToString(index->name()) << "\n";

    if (index->objectType() == Index::Object::RECORD)
    {
        foreach (SubIndex *subIndex, index->subIndexes())
        {
            hFile << "#define " << "OD_INDEX" << QString::number(index->index(), 16).toUpper() << "_" << QString::number(subIndex->subIndex(), 16);
            hFile << " OD_RAM." << varNameToString(subIndex->name()) << "\n";
        }
    }

    else if (index->objectType() == Index::Object::ARRAY)
    {
        foreach (SubIndex *subIndex, index->subIndexes())
        {
            uint8_t numSubIndex = subIndex->subIndex();

            if (numSubIndex == 0)
                continue;

            hFile << "#define " << "OD_INDEX" << QString::number(index->index(), 16).toUpper() << "_" << QString::number(numSubIndex, 16).toUpper();
            hFile << " OD_RAM." << varNameToString(index->name()) << "[" << QString::number(numSubIndex - 1, 16).toUpper() << "]" << "\n";
        }
    }

    hFile << "\n";
}
