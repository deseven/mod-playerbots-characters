#ifndef MOD_PBC_WMO_AREAS_H
#define MOD_PBC_WMO_AREAS_H

#include <string>
#include <map>
#include <cstdint>

// ---------------------------------------------------------------------------
// Supplemental WMO area name lookup
//
// The server's WMOAreaTableEntry does not load the name fields from the DBC
// (they are commented out in DBCStructure.h and skipped in the format string).
// This helper parses WMOAreaTable.dbc directly at startup to build a lookup
// of (rootId, adtId, groupId) -> area name, which we use to enhance location
// descriptions for indoor subzones like "Lion's Pride Inn".
//
// Names are read for the server's DBC locale: first from the per-locale file
// (dbc/<locale>/WMOAreaTable.dbc, e.g. dbc/ruRU/...), falling back to the
// enUS column of the base dbc/WMOAreaTable.dbc for entries without a
// translation.
// ---------------------------------------------------------------------------

// Key: (rootId, adtId, groupId) triple identifying a WMO group
struct PBC_WmoAreaKey
{
    int32_t rootId;
    int32_t adtId;
    int32_t groupId;

    bool operator<(PBC_WmoAreaKey const& o) const
    {
        if (rootId != o.rootId) return rootId < o.rootId;
        if (adtId != o.adtId) return adtId < o.adtId;
        return groupId < o.groupId;
    }
};

// Global WMO area name map, populated at startup.
extern std::map<PBC_WmoAreaKey, std::string> g_PBC_WmoAreaNames;

// Parse WMOAreaTable.dbc and populate g_PBC_WmoAreaNames.
// Returns the number of entries loaded, or 0 on failure (non-fatal).
uint32_t PBC_LoadWMOAreaNames();

// Look up the WMO area name for the given triple.
// Returns empty string if not found.
std::string PBC_GetWmoAreaName(int32_t rootId, int32_t adtId, int32_t groupId);

#endif // MOD_PBC_WMO_AREAS_H
