// Compatibility contract rules (Compatibility.h). AGPL-3.0-only.
#include "../Compatibility.h"
#include <cassert>
#include <iostream>

using namespace Compatibility;

static bool Has(const std::vector<std::string>& problems, const std::string& part) {
    for (const auto& p : problems) if (p.find(part) != std::string::npos) return true;
    return false;
}

int main() {
    const std::string arenaHash(64, 'a'), frostHash(64, 'b');
    const std::string document =
        "vibeless-requirements 1\r\n"
        "foundation 0.1.0\n"
        "feature deepfrost\n"
        "feature test-arena\n"
        "\n"
        "package backport.deepfrost-gnasher 1.0.0\n"
        "file backport.deepfrost-gnasher Frost144_Hunt_P.pak " + frostHash + "\n"
        "package backport.test-arena 1.0.0\n"
        "file backport.test-arena ZZ_TestArena144_P.pak " + arenaHash + "\n";
    Requirements req;
    assert(ParseRequirements(document, req).empty());
    assert(req.Foundation == "0.1.0" && req.Features.size() == 2 && req.Files.size() == 2 && req.Packages.size() == 2);

    // Malformed documents are refused with the line.
    Requirements bad;
    assert(ParseRequirements("hello\n", bad) == "not a vibeless-requirements 1 document");
    assert(ParseRequirements("vibeless-requirements 1\nfile x A.pak " + frostHash + "\n", bad).find("line 2") == 0);
    assert(!ParseRequirements("vibeless-requirements 1\npackage p 1.0.0\nfile p ../evil.pak " + frostHash + "\n", bad).empty());
    assert(!ParseRequirements("vibeless-requirements 1\npackage p 1.0.0\nfile p A.pak ABC\n", bad).empty());
    assert(!ParseRequirements("vibeless-requirements 1\nfeature a b\n", bad).empty());
    assert(!ParseRequirements("vibeless-requirements 1\nunknown x\n", bad).empty());
    assert(!ParseRequirements("vibeless-requirements 1\npackage p 1.0\n", bad).empty());
    assert(!ParseRequirements("vibeless-requirements 1\npackage p 1.0.0\npackage p 1.0.0\n", bad).empty());

    // A matching client has no problems; patch versions of the foundation may differ.
    ClientState client{"0.1.3", {"deepfrost", "test-arena"}, {{"Frost144_Hunt_P.pak", frostHash}, {"ZZ_TestArena144_P.pak", arenaHash}}};
    assert(Check(req, client).empty());

    // Each kind of mismatch is named for the player.
    auto wrong = client;
    wrong.Foundation = "0.2.0";
    wrong.Features.erase("test-arena");
    wrong.Features.insert("thunderdeep");
    wrong.Files["Frost144_Hunt_P.pak"] = std::string(64, 'c');
    wrong.Files["ZZ_TestArena144_P.pak"] = "";
    const auto problems = Check(req, wrong);
    assert(problems.size() == 5);
    assert(Has(problems, "The server runs Vibeless 0.1.0; this game has Vibeless 0.2.0."));
    assert(Has(problems, "native feature \"test-arena\", which this game does not run"));
    assert(Has(problems, "This game runs the native feature \"thunderdeep\", which the server does not use."));
    assert(Has(problems, "backport.deepfrost-gnasher 1.0.0: Frost144_Hunt_P.pak is a different version."));
    assert(Has(problems, "backport.test-arena 1.0.0: ZZ_TestArena144_P.pak is missing."));

    // A core server accepts a core client, and refuses a client that runs features.
    Requirements core;
    assert(ParseRequirements("vibeless-requirements 1\nfoundation 0.1.0\n", core).empty());
    assert(Check(core, ClientState{"0.1.0", {}, {}}).empty());
    assert(Check(core, ClientState{"0.1.0", {"deepfrost"}, {}}).size() == 1);

    // Report format.
    assert(Report(ClientState{"0.1.0", {"deepfrost"}, {{"A.pak", ""}, {"B.pak", frostHash}}})
        == "vibeless-client 1\nfoundation 0.1.0\nfeature deepfrost\nfile A.pak missing\nfile B.pak " + frostHash + "\n");
    assert(MajorMinor("0.1.7") == "0.1");

    std::cout << "compatibility tests passed" << std::endl;
    return 0;
}
