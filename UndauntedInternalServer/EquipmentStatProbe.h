// Included inside EquipmentProbe. Inspect loaded table rows and computed catalog stats only.
template<class T> inline const T* StatRow(UDataTable* table, const char* id, const char* rowType) {
    if (!table || !table->RowStruct || table->RowStruct->GetName() != rowType
        || table->RowStruct->Size != static_cast<int>(sizeof(T)) || !table->RowMap.IsValid()
        || table->RowMap.NumAllocated() < 0 || table->RowMap.NumAllocated() > 4096
        || table->RowMap.Max() < table->RowMap.NumAllocated()
        || table->RowMap.GetAllocationFlags().Num() < table->RowMap.NumAllocated()
        || table->RowMap.GetAllocationFlags().Max() < table->RowMap.GetAllocationFlags().Num()) return nullptr;
    for (int i = 0; i < table->RowMap.NumAllocated(); ++i)
        if (table->RowMap.IsValidIndex(i) && table->RowMap[i].Key().ToString() == id)
            return reinterpret_cast<const T*>(table->RowMap[i].Value());
    return nullptr;
}
inline std::string Overrides(const TArray<FPowerPerLevelOverride>& values) {
    if (!DraskProbe::Bounded(values, 32)) return "invalid";
    std::string json = "[";
    for (int i = 0; i < values.Num(); ++i) {
        if (!std::isfinite(values[i].NewPerLevelValue)) return "invalid";
        if (i) json += ',';
        json += "{\"startingLevel\":" + std::to_string(values[i].StartingLevel)
            + ",\"perLevel\":" + DraskProbe::Number(values[i].NewPerLevelValue) + "}";
    }
    return json + ']';
}
inline std::string ComputedStats(UEquipmentCatalogItem* item, int level) {
    if (!item) return "invalid";
    const auto stats = item->GetGearStats(level);
    if (!DraskProbe::Bounded(stats, 16)) return "invalid";
    std::string json = "[";
    for (int i = 0; i < stats.Num(); ++i) {
        if (!std::isfinite(stats[i].Magnitude) || !DraskProbe::Bounded(stats[i].Attributes, 32)) return "invalid";
        if (i) json += ',';
        json += "{\"magnitude\":" + DraskProbe::Number(stats[i].Magnitude) + ",\"attributes\":[";
        for (int j = 0; j < stats[i].Attributes.Num(); ++j) {
            const auto& attribute = stats[i].Attributes[j];
            if (!DraskProbe::Bounded<wchar_t>(attribute.AttributeName, 256)) return "invalid";
            if (j) json += ',';
            json += DojoProbe::Json(attribute.AttributeName.ToString());
        }
        json += "]}";
    }
    return json + ']';
}
inline std::string WeaponStatSummary(const FWeaponPowerTableData* row) {
    if (!row) return "invalid";
    for (float value : {row->GlobalPower,row->GlobalPowerPerLevel,row->ElementalPower,
        row->ElementalPowerPerLevel,row->ShockPower,row->ShockPowerPerLevel,row->BaseWeaponElementalHealthDamageMod})
        if (!std::isfinite(value)) return "invalid";
    const auto globalOverrides = Overrides(row->GlobalPowerPerLevelOverrides);
    const auto elementalOverrides = Overrides(row->ElementalPowerPerLevelOverrides);
    if (globalOverrides == "invalid" || elementalOverrides == "invalid") return "invalid";
    return "{\"global\":" + DraskProbe::Number(row->GlobalPower)
        + ",\"perLevel\":" + DraskProbe::Number(row->GlobalPowerPerLevel)
        + ",\"physical\":" + std::to_string(static_cast<int>(row->PhysicalType))
        + ",\"element\":" + std::to_string(static_cast<int>(row->ElementalType))
        + ",\"elemental\":" + DraskProbe::Number(row->ElementalPower)
        + ",\"elementalPerLevel\":" + DraskProbe::Number(row->ElementalPowerPerLevel)
        + ",\"shock\":" + DraskProbe::Number(row->ShockPower)
        + ",\"shockPerLevel\":" + DraskProbe::Number(row->ShockPowerPerLevel)
        + ",\"elementalHealthDamageMod\":" + DraskProbe::Number(row->BaseWeaponElementalHealthDamageMod)
        + ",\"globalOverrides\":" + globalOverrides + ",\"elementalOverrides\":" + elementalOverrides + '}';
}
inline std::string ArmorStatSummary(const FArmourResistanceTableData* row) {
    if (!row) return "invalid";
    for (float value : {row->GlobalResistance,row->GlobalResistancePerLevel,row->FlameResistance,
        row->FrostResistance,row->ShockResistance,row->ShockResistancePerLevel,row->RadiantResistance,
        row->UmbralResistance,row->TerraResistance}) if (!std::isfinite(value)) return "invalid";
    const auto globalOverrides = Overrides(row->GlobalResistancePerLevelOverrides);
    const auto elementalOverrides = Overrides(row->ElementalResistancePerLevelOverrides);
    if (globalOverrides == "invalid" || elementalOverrides == "invalid") return "invalid";
    return "{\"global\":" + DraskProbe::Number(row->GlobalResistance)
        + ",\"perLevel\":" + DraskProbe::Number(row->GlobalResistancePerLevel)
        + ",\"flame\":" + DraskProbe::Number(row->FlameResistance)
        + ",\"frost\":" + DraskProbe::Number(row->FrostResistance)
        + ",\"shock\":" + DraskProbe::Number(row->ShockResistance)
        + ",\"shockPerLevel\":" + DraskProbe::Number(row->ShockResistancePerLevel)
        + ",\"radiant\":" + DraskProbe::Number(row->RadiantResistance)
        + ",\"umbral\":" + DraskProbe::Number(row->UmbralResistance)
        + ",\"terra\":" + DraskProbe::Number(row->TerraResistance)
        + ",\"globalOverrides\":" + globalOverrides + ",\"elementalOverrides\":" + elementalOverrides + '}';
}
inline bool CheckStatPair(UWorld* world, const StatPair& pair) {
    auto* tuning = UArchonGameplayStatics::GetArchonGlobalTuning(world);
    auto* catalog = UArchonGameplayStatics::GetArchonCatalog(world);
    const std::wstring id(pair.id, pair.id + std::strlen(pair.id));
    const std::wstring oldId(pair.sourceId, pair.sourceId + std::strlen(pair.sourceId));
    auto* item = catalog ? catalog->GetEquipmentCatalogItem(UKismetStringLibrary::Conv_StringToName(id.c_str())) : nullptr;
    auto* oldItem = catalog ? catalog->GetEquipmentCatalogItem(UKismetStringLibrary::Conv_StringToName(oldId.c_str())) : nullptr;
    bool valid = tuning && item && oldItem && static_cast<int>(item->GetMaxUpgradeLevel()) == 15
        && static_cast<int>(item->GetStartingUpgradeLevel()) == 0 && !item->IsCosmeticOnly();
    Log("\"event\":\"stat_pair_begin\",\"itemId\":" + DojoProbe::Json(pair.id)
        + ",\"sourceId\":" + DojoProbe::Json(pair.sourceId) + ",\"catalogReady\":" + DojoProbe::Bool(valid));
    if (!valid) return false;
    for (int level : {0,1,2,6,10,15}) {
        auto* table = pair.armor ? tuning->GetArmourResistanceTable(static_cast<EUpgradeableItemLevel>(level))
            : tuning->GetWeaponPowerTable(static_cast<EUpgradeableItemLevel>(level));
        const auto current = pair.armor
            ? ArmorStatSummary(StatRow<FArmourResistanceTableData>(table, pair.id, "ArmourResistanceTableData"))
            : WeaponStatSummary(StatRow<FWeaponPowerTableData>(table, pair.id, "WeaponPowerTableData"));
        const auto prior = pair.armor
            ? ArmorStatSummary(StatRow<FArmourResistanceTableData>(table, pair.sourceId, "ArmourResistanceTableData"))
            : WeaponStatSummary(StatRow<FWeaponPowerTableData>(table, pair.sourceId, "WeaponPowerTableData"));
        const auto computed = ComputedStats(item, level), oldComputed = ComputedStats(oldItem, level);
        const bool matched = current != "invalid" && prior != "invalid" && current == prior
            && computed != "invalid" && oldComputed != "invalid" && computed == oldComputed;
        Log("\"event\":\"stat_level\",\"itemId\":" + DojoProbe::Json(pair.id)
            + ",\"sourceId\":" + DojoProbe::Json(pair.sourceId) + ",\"level\":" + std::to_string(level)
            + ",\"table\":" + DraskProbe::Object(table) + ",\"matched\":" + DojoProbe::Bool(matched)
            + ",\"row\":" + (current == "invalid" ? "null" : current)
            + ",\"computed\":" + (computed == "invalid" ? "null" : computed));
        if (!matched) return false;
    }
    return true;
}
