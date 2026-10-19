// Exercise the production SDK container, with a relocating, tracked engine allocator.
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <malloc.h>
#include <unordered_map>
#include "../UnrealContainers.hpp"
#include "../SDK/Archon_structs.hpp"

namespace {
std::unordered_map<void*, size_t> allocations;
int invalidFrees = 0;
bool failAllocation = false;
void* Allocate(void* old, UC::uint64 bytes, UC::uint32 alignment) {
    if (old && !allocations.contains(old)) { ++invalidFrees; return nullptr; }
    if (bytes && failAllocation) throw std::bad_alloc();
    void* next = bytes ? _aligned_malloc(static_cast<size_t>(bytes), (std::max)(size_t(alignment), alignof(max_align_t))) : nullptr;
    if (bytes && !next) throw std::bad_alloc();
    if (next) std::memset(next, 0xCD, static_cast<size_t>(bytes));
    if (old) {
        if (next) std::memcpy(next, old, (std::min)(allocations.at(old), static_cast<size_t>(bytes)));
        allocations.erase(old);
        _aligned_free(old);
    }
    if (next) allocations.emplace(next, static_cast<size_t>(bytes));
    return next;
}
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Row {
    UC::FString name;
    UC::TArray<UC::FString> maps;
};
// Model a ProcessEvent output: storage and live elements are populated by Unreal.
struct EngineOutput : UC::TArray<Row> {
    EngineOutput() : TArray(1) {
        new (Data) Row{UC::FString(L"Deepfrost"), {}};
        Data[0].maps.Add(UC::FString(L"snow"));
        NumElements = 1;
    }
};
struct ThrowingValue {
    static inline int alive = 0, copiesBeforeThrow = -1;
    int value;
    explicit ThrowingValue(int v) : value(v) { ++alive; }
    ThrowingValue(const ThrowingValue& other) : value(other.value) {
        if (copiesBeforeThrow == 0) throw std::runtime_error("expected copy failure");
        if (copiesBeforeThrow > 0) --copiesBeforeThrow;
        ++alive;
    }
    ThrowingValue& operator=(const ThrowingValue&) = default;
    ~ThrowingValue() { --alive; }
};
void Run(const std::string& name) {
    if (name == "copy-reused") {
        UC::TArray<unsigned> source; source.Add(0x11223344); source.Add(0x55667788);
        UC::TArray<unsigned> target(8); target.Add(0xAAAAAAAA); target.Add(0xBBBBBBBB);
        target = source;
        Check(target.Num() == 2 && target[0] == source[0] && target[1] == source[1], "copy into reused storage corrupted values");
    } else if (name == "empty-copy") {
        UC::TArray<unsigned> target; target.Add(7); UC::TArray<unsigned> empty; target = empty;
        Check(target.Num() == 0, "empty assignment retained elements");
    } else if (name == "free-twice") {
        UC::TArray<unsigned> target; target.Add(7); target.Free(); target.Free();
        Check(target.Num() == 0 && target.Max() == 0, "Free did not reset state");
        target.Add(9); Check(target[0] == 9, "reuse after Free failed");
    } else if (name == "engine-output") {
        EngineOutput output;
        Check(output[0].maps[0].ToWString() == L"snow", "invalid engine output");
    } else if (name == "strings-copy") {
        UC::TArray<UC::FString> source; source.Add(UC::FString(L"Thunderdeep"));
        UC::TArray<UC::FString> target(8); target.Add(UC::FString(L"old")); target = source;
        Check(target[0].CStr() != source[0].CStr(), "string copy aliases source allocation");
        source[0].CStr()[0] = L'X';
        Check(target[0].ToWString() == L"Thunderdeep", "copy changed with source");
    } else if (name == "nested-copy") {
        EngineOutput source; UC::TArray<Row> target = source;
        Check(target[0].maps[0].CStr() != source[0].maps[0].CStr(), "nested copy aliases source");
        source.Free(); Check(target[0].maps[0].ToWString() == L"snow", "nested copy did not retain values");
    } else if (name == "clear-remove") {
        UC::TArray<UC::FString> values; values.Add(UC::FString(L"a")); values.Add(UC::FString(L"b")); values.Add(UC::FString(L"c"));
        values.Remove(1); Check(values.Num() == 2 && values[1].ToWString() == L"c", "remove lost remaining value");
        values.Remove(-1); values.Remove(10); Check(values.Num() == 2, "invalid removal changed array");
        values.Clear(); Check(values.Num() == 0 && allocations.size() == 1, "Clear leaked element allocations");
        values.Add(UC::FString(L"reused")); Check(values[0].ToWString() == L"reused", "reuse after Clear failed");
    } else if (name == "alias-growth") {
        UC::TArray<UC::FString> values(1); values.Add(UC::FString(L"alias")); values.Add(values[0]);
        Check(values.Num() == 2 && values[0].ToWString() == L"alias" && values[1].ToWString() == L"alias", "aliased append failed during growth");
        values.Reserve(20); Check(values[1].ToWString() == L"alias", "Reserve lost a live string");
    } else if (name == "move-self") {
        UC::TArray<UC::FString> values; values.Add(UC::FString(L"value")); values = values;
        values = std::move(values); UC::TArray<UC::FString> moved(std::move(values));
        Check(values.Num() == 0 && values.Max() == 0 && moved[0].ToWString() == L"value", "move state incorrect");
        values.Add(UC::FString(L"replacement")); values = std::move(moved);
        Check(moved.Num() == 0 && values[0].ToWString() == L"value", "move assignment failed");
    } else if (name == "throwing-copy") {
        UC::TArray<ThrowingValue> source(2); source.Add(ThrowingValue(1)); source.Add(ThrowingValue(2));
        UC::TArray<ThrowingValue> target; target.Add(ThrowingValue(9));
        ThrowingValue::copiesBeforeThrow = 1;
        try { target = source; Check(false, "copy should throw"); } catch (const std::runtime_error&) {}
        ThrowingValue::copiesBeforeThrow = -1;
        Check(target.Num() == 1 && target[0].value == 9 && ThrowingValue::alive == 3, "failed copy damaged destination or leaked elements");
    } else if (name == "sdk-hunt-polling") {
        static_assert(sizeof(SDK::FHunt_MapInfo) == 0x80);
        static_assert(sizeof(SDK::FMatchmakerHuntTableData) == 0x1C8);
        for (int tick = 0; tick < 2000; ++tick) {
            {
                SDK::FHunt_MapInfo map{};
                map.MapName = UC::FString(L"Deepfrost Gnasher");
                map.MapAssetName = UC::FString(L"/Game/Maps/islands/1609/bella_jen_snow");
                map.GatherableDistributions.Add(SDK::FHunt_GatherableDistributionWeight{});
                SDK::FMatchmakerHuntTableData engineResult{};
                engineResult.MapList.Add(map);
                SDK::FMatchmakerHuntTableData returned = std::move(engineResult);
                SDK::FMatchmakerHuntTableData copied = returned;
                returned.MapList.Clear();
                Check(copied.MapList[0].MapName.ToWString() == L"Deepfrost Gnasher", "SDK hunt map copy lost name");
                Check(copied.MapList[0].GatherableDistributions.Num() == 1, "SDK hunt map lost nested distribution");
            }
            Check(allocations.empty(), "SDK hunt polling leaked nested allocation");
        }
    } else if (name == "throwing-reserve") {
        UC::TArray<ThrowingValue> values(2); values.Add(ThrowingValue(1)); values.Add(ThrowingValue(2));
        ThrowingValue::copiesBeforeThrow = 1;
        bool threw = false;
        try { values.Reserve(10); } catch (const std::runtime_error&) { threw = true; }
        ThrowingValue::copiesBeforeThrow = -1;
        Check(threw && values.Num() == 2 && values.Max() == 2 && values[0].value == 1 && values[1].value == 2
            && ThrowingValue::alive == 2, "failed Reserve damaged values or leaked objects");
    } else if (name == "allocation-failure") {
        UC::TArray<unsigned> values(1); values.Add(42); failAllocation = true;
        try { values.Reserve(10); Check(false, "allocation should throw"); } catch (const std::bad_alloc&) {}
        failAllocation = false;
        Check(values.Num() == 1 && values.Max() == 1 && values[0] == 42, "allocation failure damaged array");
    } else throw std::runtime_error("unknown test");
}
}
int main(int argc, char** argv) {
    static_assert(sizeof(UC::TArray<unsigned>) == 16 && sizeof(UC::FString) == 16);
    UC::FMemory::EngineRealloc = Allocate;
    try {
        if (argc != 2) throw std::runtime_error("one test name required");
        Run(argv[1]);
        Check(invalidFrees == 0, "duplicate/invalid engine free");
        Check(allocations.empty(), "leaked engine allocation");
        Check(ThrowingValue::alive == 0, "leaked live objects");
        std::cout << "PASS " << argv[1] << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << (argc > 1 ? argv[1] : "arguments") << ": " << error.what() << '\n'; return 1;
    }
}
