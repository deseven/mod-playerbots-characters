#include "pbc_wmo_areas.h"
#include "pbc_log.h"
#include "World.h"
#include "Common.h"

#include <fstream>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// DBC binary format constants for WMOAreaTable
//
// Record layout (28 fields × 4 bytes = 112 bytes per record):
//   Field  0: uint32 ID
//   Field  1: int32  rootId
//   Field  2: int32  adtId
//   Field  3: int32  groupId
//   Fields 4-8: uint32 (unused)
//   Field  9: uint32 Flags
//   Field 10: uint32 areaId
//   Fields 11-26: uint32 string offsets (16 locales) into string block
//   Field 27: uint32 name flags
//
// In this DBC distribution the 16 name columns are split across files: the
// base dbc/WMOAreaTable.dbc contains only the en-US column (field 11), while
// per-locale copies live in dbc/<locale>/WMOAreaTable.dbc and populate the
// column matching that locale (field 11 + locale index).  We read the column
// matching the server's DBC locale from the locale-specific file, with
// fallback to the enUS column of the base file for missing translations.
// ---------------------------------------------------------------------------

static constexpr uint32_t WMO_DBC_FIELD_COUNT = 28;
static constexpr uint32_t WMO_DBC_RECORD_SIZE = WMO_DBC_FIELD_COUNT * 4;
static constexpr uint32_t WMO_DBC_STRING_OFFSET_FIELD = 11;
static constexpr uint32_t WMO_DBC_LOCALE_COUNT = 16;

std::map<PBC_WmoAreaKey, std::string> g_PBC_WmoAreaNames;

namespace
{
    // Parse one WMOAreaTable.dbc file, storing names from the given locale column.
    // Returns the number of names stored into the map by this call.
    // `preferExisting` keeps already-stored names on duplicate keys (used to fill
    // enUS fallbacks without overwriting the localized ones).
    uint32_t ParseWmoAreaDbc(std::string const& dbcPath, uint32_t localeField, bool preferExisting)
    {
        std::ifstream file(dbcPath, std::ios::binary);
        if (!file.is_open())
            return 0;

        char magic[4];
        uint32_t recordCount, fieldCount, recordSize, stringBlockSize;
        file.read(magic, 4);
        if (std::memcmp(magic, "WDBC", 4) != 0)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "WMOAreaTable.dbc at '{}' has invalid magic — skipping.", dbcPath);
            return 0;
        }

        file.read(reinterpret_cast<char*>(&recordCount), 4);
        file.read(reinterpret_cast<char*>(&fieldCount), 4);
        file.read(reinterpret_cast<char*>(&recordSize), 4);
        file.read(reinterpret_cast<char*>(&stringBlockSize), 4);

        if (fieldCount != WMO_DBC_FIELD_COUNT)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "WMOAreaTable.dbc at '{}' has unexpected field count {} (expected {}) — skipping.", dbcPath, fieldCount, WMO_DBC_FIELD_COUNT);
            return 0;
        }

        if (recordSize != WMO_DBC_RECORD_SIZE)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "WMOAreaTable.dbc at '{}' has unexpected record size {} (expected {}) — skipping.", dbcPath, recordSize, WMO_DBC_RECORD_SIZE);
            return 0;
        }

        // Read all records + string block in one go
        uint32_t recordsSize = recordCount * recordSize;
        std::vector<char> data(recordsSize + stringBlockSize);
        file.read(data.data(), data.size());
        if (!file)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "WMOAreaTable.dbc at '{}' read error — skipping.", dbcPath);
            return 0;
        }

        const char* stringBlock = data.data() + recordsSize;

        uint32_t loaded = 0;
        for (uint32_t i = 0; i < recordCount; ++i)
        {
            const char* rec = data.data() + i * recordSize;

            int32_t rootId, adtId, groupId;
            std::memcpy(&rootId,   rec + 1 * 4, 4);
            std::memcpy(&adtId,    rec + 2 * 4, 4);
            std::memcpy(&groupId,  rec + 3 * 4, 4);

            uint32_t strOffset;
            std::memcpy(&strOffset, rec + localeField * 4, 4);

            if (strOffset == 0 || strOffset >= stringBlockSize)
                continue; // no name for this locale

            const char* name = stringBlock + strOffset;
            if (name[0] == '\0')
                continue; // empty string

            PBC_WmoAreaKey key{rootId, adtId, groupId};
            if (preferExisting)
                g_PBC_WmoAreaNames.try_emplace(key, name);
            else
                g_PBC_WmoAreaNames[key] = name;
            ++loaded;
        }

        return loaded;
    }
} // namespace

uint32_t PBC_LoadWMOAreaNames()
{
    g_PBC_WmoAreaNames.clear();

    std::string dataPath = sWorld->GetDataPath();
    LocaleConstant locale = sWorld->GetDefaultDbcLocale();

    std::string basePath = dataPath + "dbc/WMOAreaTable.dbc";
    std::string localizedPath;
    if (locale != LOCALE_enUS)
        localizedPath = dataPath + "dbc/" + localeNames[locale] + "/WMOAreaTable.dbc";

    uint32_t localeField = WMO_DBC_STRING_OFFSET_FIELD + static_cast<uint32_t>(locale);
    if (localeField >= WMO_DBC_STRING_OFFSET_FIELD + WMO_DBC_LOCALE_COUNT)
        localeField = WMO_DBC_STRING_OFFSET_FIELD; // invalid locale, use enUS column

    // 1) The per-locale file (e.g. dbc/ruRU/WMOAreaTable.dbc) is the
    //    authoritative source for the server's DBC locale.
    if (!localizedPath.empty())
    {
        std::ifstream probe(localizedPath, std::ios::binary);
        bool hasLocalized = probe.is_open();
        if (hasLocalized)
            ParseWmoAreaDbc(localizedPath, localeField, false);
    }

    // 2) The base file: for enUS (or invalid locale) it's the authoritative
    //    source; for other locales its server-locale column is normally empty
    //    (split-file layout), so it only serves as fallback below.
    if (locale == LOCALE_enUS || localeField == WMO_DBC_STRING_OFFSET_FIELD)
        ParseWmoAreaDbc(basePath, localeField, false);

    // 3) Fall back to the base file's enUS column for records that have no
    //    translation in the server's locale (fill only missing keys).
    if (locale != LOCALE_enUS)
        ParseWmoAreaDbc(basePath, WMO_DBC_STRING_OFFSET_FIELD, true);

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Loaded {} WMO area names from WMOAreaTable.dbc (locale {}).", g_PBC_WmoAreaNames.size(), GetNameByLocaleConstant(locale));
    return static_cast<uint32_t>(g_PBC_WmoAreaNames.size());
}

std::string PBC_GetWmoAreaName(int32_t rootId, int32_t adtId, int32_t groupId)
{
    // Try exact match first
    auto it = g_PBC_WmoAreaNames.find(PBC_WmoAreaKey{rootId, adtId, groupId});
    if (it != g_PBC_WmoAreaNames.end() && !it->second.empty())
        return it->second;

    // Fall back to groupId=-1 wildcard entry (used in DBC as default name for all groups in a WMO root/ADT)
    it = g_PBC_WmoAreaNames.find(PBC_WmoAreaKey{rootId, adtId, -1});
    if (it != g_PBC_WmoAreaNames.end() && !it->second.empty())
        return it->second;

    return {};
}
