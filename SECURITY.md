# Security policy: Vibeless

## Supported versions

| Version | Supported |
| --- | --- |
| The latest commit on `main` | Yes |
| Older commits and bundles built from them | No. Check that the problem is still there on the latest commit first. |
| The `upstream/dauntless-revived` mirror branch | No. Report problems in upstream code to [Dauntless Revived](https://github.com/mixutin/dauntless-revived/security) or [Undaunted](https://github.com/SyST3MDeV/Undaunted). |

## How to report

Report privately through GitHub's private vulnerability reporting:
**[Report a vulnerability](https://github.com/Kayatwo/Vibeless/security/advisories/new)** (the
repository's **Security** tab, then **Report a vulnerability**). Only you and the maintainer can see
the report. Please do not open a public issue, discussion or pull request about a vulnerability
until a fix is available.

Include the commit you tested, what the problem is and what an attacker could do with it, how to
reproduce it, and how you would like to be credited. Redact keys, tokens and other people's data.

If the problem is in code Vibeless inherits unchanged from Dauntless Revived or Undaunted, it is
worth reporting there too; say so in your report and I will coordinate.

## Scope and expectations

Vibeless is a hobby project maintained by one person; responses are best effort. Test only on
your own installation and accounts, never on servers or accounts that are not yours. Good-faith
research that follows this policy will not be pursued.

In scope: the native runtime, the deploy server and metagame, the compatibility contract, the bundle
tooling and CI. Out of scope: the Dauntless game client itself, and anything that needs game files
or cheats against other players.
