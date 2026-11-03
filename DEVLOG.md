# Revive steamframe devlog

Nov 3, 2026 · @Gabor Bartko

Online változat (ez az elsődleges, ez a fájl annak exportja): https://claude.ai/artifact/Tea8v4BbRGpEwvk1ixaHnc

## Cél

Oculus (Meta) PC VR játékokat szeretnénk Revive-val, a ReviveXR (OpenXR) backenden futtatni SteamVR alatt. A munka a `steamframe` ágon folyik, a repó helye `D:\Revive`.

A kiinduló hiba az volt, hogy a játékok szerint nincs Oculus Rift csatlakoztatva, és egyáltalán nem indultak el. Ennek fő oka: a ReviveXR OpenXR 1.1-et kért, a SteamVR pedig ezt elutasította.

## Jelenlegi állapot

Minden változás commitolva van a `steamframe` ágon (pusholva a github.com/Gabiga71/ReFrame repóba). A First Contact 2026-11-03 19:32-kor először futott végig a Frame-en: kép, hang és interakció rendben.

- Az Oculus heartbeat működik: a szerver folyamatosan `HTTP 200`-at ad vissza.
- Az overlay a legutóbbi teszten kétszer próbálta indítani az `oculus-first-contact`-ot. Akkor még nem volt telepítve, azóta (11-03 19:00 körül) fel lett telepítve.
- A rendszert a SteamVR miatt újra kell indítani a következő teszt előtt.
- Csak az `Externals/openvr` submodule-ban maradtak nem követett fájlok. Ezekhez nem nyúltunk.

## Elvégzett változások

| Komponens | Fájl | Változás | Miért |
| --- | --- | --- | --- |
| ReviveXR | `Runtime.cpp` | OpenXR 1.0-t kér az `XR_CURRENT_API_VERSION` helyett | A SteamVR az 1.1-es kérést `XR_ERROR_API_VERSION_UNSUPPORTED` hibával utasítja el |
| ReviveXR | `Runtime.cpp` | `HACK_WAIT_FOR_SESSION_READY` bekapcsolva SteamVR-re | A SteamVR 2.x futó session nélkül nem ad view adatot |
| ReviveXR | `Session.cpp` | Az ideiglenes session minden kilépési úton lezárul, a READY-re várás legfeljebb 5 s | Eddig szivárgott, és ettől `XR_ERROR_LIMIT_REACHED` + végtelen ciklus jött |
| ReviveXR | `Swapchain.cpp` | Formátumválasztás tartaléklistával | A SteamVR csak sRGB formátumokat fogad el. Mellékhatás: kifakult színek lehetnek |
| ReviveXR | `Common.cpp/.h`, `REV_CAPI.cpp` | Napló a `ReviveXR.txt` fájlba: hibás hívások, session állapotok, első frame | Eddig nem volt semmilyen napló |
| ReviveOverlay | `oculusoauthtokencontroller.cpp`, `windowsservices.cpp`, `Oculus.js` | A heartbeat WinHTTP-n megy, form-encoded törzzsel | A Qt 5-ben nincs TLS OpenSSL nélkül, és a paraméterek régen nem érkeztek meg |
| ReviveOverlay | `oculusoauthtokencontroller.cpp` | Az Oculus DB másolása a `-wal` és `-shm` fájlokkal együtt | WAL módban a fő fájl üres lehet |
| ReviveOverlay | `revivemanifestcontroller.cpp` | A nem telepített support appok kiszűrése | Az elavult bejegyzések eltakarják a telepített appokat |
| ReviveOverlay | `revivemanifestcontroller.cpp`, `trayiconcontroller.cpp` | Nincs dupla indítás, és jelennek meg tálcaértesítések | Két példány versenyez a headsetért, és mindkettő elbukik |
| ReviveInjector | `main.cpp` | A CreateProcess hibakódját és hibaüzenetét naplózza | Eddig csak „Failed to create process” állt a naplóban |
| Build | `vcpkg.json`, `vcpkg-triplets/` | vcpkg manifest (detours, openxr-loader, glfw3), statikus v142 tripletek | Reprodukálható függőségek |
| Build | `.gitignore` | Oculus SDK, `/VS/` és a qmake által generált fájlok kizárása | Ne kerüljenek a repóba |

## Commitok

Tíz új commit került a `91c5792` (Update version.h) után, mind fent van a ReFrame repóban.

| Hash | Üzenet |
| --- | --- |
| `d9f1f34` | Note that other SteamVR headsets are untested with ReFrame |
| `4077b66` | Describe the compatibility layer as targeting the Steam Frame |
| `dfbd614` | Point the README installation steps at the ReFrame releases |
| `e6c01ce` | Update the development log with the installer and release |
| `676a978` | Enable the OpenXR backend by default in the installer |
| `1a2ebc0` | Introduce ReFrame in the README and add the development log |
| `ab2de1b` | Deploy the input manifests with the build and remember the OpenXR toggle |
| `f5c0f76` | Get Unreal based Oculus games onto a Steam Frame through ReviveXR |
| `ed80a03` | Ignore qmake generated ReviveOverlay build files |
| `797be89` | Make ReviveXR work on SteamVR and harden the overlay launch path |

## Tesztelés és naplók

Mindhárom napló a `%LOCALAPPDATA%\Revive\` mappában van. A `ReviveXR.txt` hozzáfűzéssel ír, ezért egy tiszta mérés előtt törölni kell.

| Fájl | Ki írja | Mit keressünk benne |
| --- | --- | --- |
| `ReviveOverlay.txt` | ReviveOverlay | `Launching application`, `Support app is not installed`, heartbeat státusz |
| `ReviveInjector.txt` | ReviveInjector | `Failed to create process (error N)` és a munkakönyvtár |
| `ReviveXR.txt` | ReviveXR (a játék folyamatában) | A kért OpenXR verzió, a runtime neve, `FAILED:` sorok, session állapotok, `first frame reached ovr_EndFrame` |

A legutóbbi teszten (2026-11-03, 18:31-ig) csak a `ReviveOverlay.txt` jött létre. Az injector és a ReviveXR naplója hiányzik, mert a First Contact nem indult el, így ezek a komponensek nem futottak.

## Nyitott problémák és következő lépések

- [x] Egy telepített Oculus játék indítása az overlayből (ne First Contact legyen), utána a három napló átnézése
- [x] Ellenőrizni, hogy a `ReviveXR.txt` szerint OpenXR 1.0 létrejön-e, és mi a runtime neve (ha nem `SteamVR/OpenXR`, a hackek nem kapcsolnak be)
- [x] Ha van kép, de a színek kifakultak: mutable format swapchain, a view-k az eredetileg kért formátumban
- [ ] A support manifest statikus, ezért a nem telepített appok kulcsai továbbra is látszanak a SteamVR-nek, ha legalább egy app megmarad. Hosszabb távon futásidőben kellene generálni
- [ ] Megfontolni, hogy a `HACK_WAIT_FOR_SESSION_READY` minden runtime-on feltétel nélkül fusson
- [ ] Ha működik: push, majd PR a `master` ágra

## Folytatás újraindítás után

1. Indítsd el a SteamVR-t, és ellenőrizd, hogy az OpenXR runtime a SteamVR legyen.
2. Töröld a `%LOCALAPPDATA%\Revive\ReviveXR.txt` és a `ReviveInjector.txt` fájlt, ha léteznek.
3. Indítsd el a ReviveOverlay-t a `Release` buildből.
4. Indíts el egy telepített Oculus játékot a dashboardról, egyszer kattints, és várd meg a tálcaértesítést.
5. A Claude Code-ban a `D:\Revive` mappában mondd ezt: „Olvasd el a devlogot és a naplókat”, és add meg ennek a dokumentumnak a linkjét.

## Napló

| Dátum | Tétel | Röviden |
| --- | --- | --- |
| 2026-11-03 | README: Steam Frame leírás | A „Revive Compatibility Layer” bevezető mondata már nem a HTC Vive-ot és az Indexet, hanem a Steam Frame-et nevezi meg (SteamVR-en át), és jelzi, hogy más SteamVR-headsetek is működhetnek, de ReFrame-mel nincsenek tesztelve (`4077b66`, `d9f1f34`) |
| 2026-11-03 | README telepítési lépések | Az eredeti Revive README Installation része a ReFrame-re igazítva: letöltés a Gabiga71/ReFrame/releases oldalról (nem `/latest`, mert az kihagyja a pre-release-t), OpenXR-alapértelmezés, SteamVR mint OpenXR runtime, ReFrame issues link. A wiki- és kompatibilitási linkek a Revive-ra mutatnak továbbra is |
| 2026-11-03 | Telepítő (3.2.0-reframe.1) | NSIS 3.13 és GitHub CLI telepítve (winget). Teljes újrafordítás x64 + x86, overlay install lépés (SupportAssets, vrmanifestek), vc_redist.x64 (Microsoft-aláírás ellenőrizve). A telepítő alapból bekapcsolja a `UseOpenXR`-t, ha a felhasználó még nem választott (commit `676a978`). Eredmény: `ReFrame-Revive-3.2.0-reframe.1-Setup.exe`, 55 MB, 193 fájl. Kiadva pre-release-ként: https://github.com/Gabiga71/ReFrame/releases/tag/v3.2.0-reframe.1 (tag → `676a978`). Tiszta gépen még nincs tesztelve |
| 2026-11-03 | README és devlog a repóban | Döntés: a projekt nem lesz átnevezve, hogy az eredeti Revive-szerzők érdeme és története ép maradjon. A README elejére ReFrame-bevezető került (fork, cél, MIT, az eredeti szerzők érdeme), az eredeti README változatlanul alatta. A `DEVLOG.md` bekerült a repóba |
| 2026-11-03 | Push a ReFrame repóba | Új remote: `reframe` = https://github.com/Gabiga71/ReFrame. A helyi klón shallow volt, ezért a push előbb elbukott; `git fetch --unshallow origin` után a teljes Revive-történet (1807 commit) felment. Feltöltve: `steamframe` (`ab2de1b`, alapértelmezett ág) és `master` (`91c5792`). A `DEVLOG.md` helyi marad |
| 2026-11-03 | Teszt 21:08 (Lone Echo) | Működik a Frame-en: 6 perc, 31 328 frame, tiszta kilépés (`ovr_Destroy`, `ovr_Shutdown`, `ExitProcess(0)`). A `SetXrInputConfig` elsőre sikeres, nincs Home. A játék `R11G11B10_FLOAT` (26) formátumot kér, ezt a tartaléklista `R16G16B16A16_FLOAT`-ra (10) cseréli; mip proxy nem kellett. A `HACK_FORCE_FOV_FALLBACK` miatt egy ideiglenes session van |
| 2026-11-03 | Új overlay élesben | Az ab2de1b overlay linkelve és elindítva. A „Use OpenXR runtime” bekapcsolása után a registryben HKCU\Software\Revive\ReviveOverlay\UseOpenXR = true |
| 2026-11-03 | Commit f5c0f76, ab2de1b | ReviveXR-javítások (mip proxy, virtuális destruktor, FOV-gyorsítótár, közös InputManager, kezdeti irány, diagnosztika), illetve a build bemásolja az Input mappát, az overlay megjegyzi az OpenXR-kapcsolót. A tévesnek bizonyult indoklásokat a kódkommentekből kijavítottam. Az új overlay még nincs linkelve, mert a futó példány zárolja az exe-t |
| 2026-11-03 | Teszt 20:16 és 20:21 (First Contact) | Mindkét indítás jó volt (kép, kontroller rendben). A második kilépés után a Frame-en egy session nem zárult be. A PC oldalán mindkét kilépés tiszta: `STOPPING` → `xrEndSession`, `EXITING` → a játék kilép, a SteamVR „Completed simple quit transition”. Utána 20:22:54-kor a Frame `Universe 424242`-re váltott, 20:22:58-kor a kapcsolat inaktív lett. Nyitott kérdés, mi ragadt be a Frame-en |
| 2026-11-03 | Teszt 20:09 (First Contact) | Működik a Frame-en: a `Release\Input` bemásolása után a SteamVR elfogadta a kontroller-konfigurációt (`SetXrInputConfig: Loading 2 bindings`), nem kapcsolt Home-ra, az irány jó. A korábbi „session-verseny” diagnózis téves volt, a kapcsolatbontást a sikertelen `SetXrInputConfig` okozta. Egy sikeres futás még nem bizonyíték, további tesztek kellenek |
| 2026-11-03 | Teszt 20:03 + Input mappa | A közös InputManager nem segített, ugyanaz a `SetXrInputConfig` hiba. Új nyom: a vrmanifest minden apphoz `action_manifest_path: Input/action_manifest.json`-t ír (`Oculus.js`), de a `Release\Input` mappa hiányzott (a telepítő másolja be), a SteamVR minden indításkor `ActionManifestError_FileNotFound`-ot ír. A `Revive/Input/*.json` bemásolva a `Release\Input`-ba. Megjegyzés: az OpenVR-es manifest is `/actions/touch` action setet használ, más action-nevekkel, mint a ReviveXR |
| 2026-11-03 | Javítás (közös InputManager) | Az action setek és binding-javaslatok OpenXR-instance-onként egyszer jönnek létre, minden `ovr_Create` ugyanazt használja; az `ovr_Shutdown` az instance előtt bontja le. Az input-hívások hibái mostantól a naplóba kerülnek. Lefordítva, nincs commitolva |
| 2026-11-03 | Teszt 19:48 és 19:53 (First Contact) | Az iránybeállítás lefutott, de a kép megint nem ment ki. Ok: a SteamVR-ben `SetXrInputConfig failed. Failed to load manifest`, utána 0,2 s-mal bontja a kapcsolatot, és a játék nem-scene appként jön vissza. Ez minden képtelen futásnál ott van (19:07, 19:14, 19:22, 19:48, 19:53), a két sikeresnél (18:59, 19:32) nincs. Gyanú: a második `ovr_Create` újra létrehozza az azonos nevű action seteket |
| 2026-11-03 | Javítás (kezdeti irány) | Session-induláskor a padlószintű origó a headset irányába fordul (csak yaw), a középpontja a play area közepén marad. Lefordítva, nincs commitolva |
| 2026-11-03 | Teszt 19:31 (First Contact) | Működik a Frame-en: kép, hang, interakció rendben, 9 perc, 50 612 frame, tiszta kilépés. Egy hiba maradt: a kezdeti irány közel 180°-kal el volt fordulva (a padlószintű tér a SteamVR play area előre-irányát követte) |
| 2026-11-03 | Javítás (session-verseny) | A FOV-lekérdező ideiglenes session folyamatonként csak egyszer jön létre, az eredményt a ReviveXR eltárolja. Az Unreal második `ovr_Create`-je így nem nyit új ideiglenes sessiont közvetlenül a valódi előtt. Lefordítva, nincs commitolva |
| 2026-11-03 | Teszt 19:22 (First Contact) | A mip proxy működik: a játék fut, rajzol, a fejkövetés és a hang megy, de a kép csak a monitoron látszik. A SteamVR minden `xrCreateSession`-t scene app indulásnak, minden `xrDestroySession`-t kilépésnek vesz. A második ideiglenes session lebontása után a valódi session kapcsolata megszakadt, a játék „generált” instance-ként csatlakozott vissza, és 10 s után a SteamVR a saját Home nézetét tette a headsetre |
| 2026-11-03 | Javítás (mip proxy) | A 19:14-es napló igazolta: a SteamVR 1 mip-szintes képet ad 5 helyett, a játék a mip 1-re kér nézetet. Javítás: ilyenkor a D3D11 swapchain saját, teljes mip-láncú proxy textúrát ad a játéknak, és commitkor a SteamVR képére másolja. Az ősosztály virtuális destruktort kapott (a D3D12 cleanup eddig nem futott). Lefordítva, nincs commitolva |
| 2026-11-03 | Teszt 19:07 (First Contact) | Meglett a kilépés oka: a játék egy 16×16-os, 5 mip-szintű swapchainre `B8G8R8A8_UNORM` RTV-t kér, ezt a D3D11 `E_INVALIDARG`-gal elutasítja, és az Unreal fatális hibával (kód 1) kilép. A textúra `B8G8R8A8_TYPELESS`, így nem a formátum a gond. Gyanú: a SteamVR kevesebb mip-szintet hoz létre. További naplópontok kellenek a megerősítéshez |
| 2026-11-03 | Diagnosztika | Új naplópontok a ReviveXR-ben: swapchain-formátumok, sikertelen D3D11-nézetek, kivételek, `ExitProcess`/`TerminateProcess` hívója. Lefordítva, nincs commitolva |
| 2026-11-03 | Teszt (First Contact, OpenXR) | Az injektálás sikerült, OpenXR 1.0 a SteamVR/OpenXR 2.18.4-en, a session létrejött. A játék a swapchain létrehozása után 0,4 s-mal csendben kilépett (87 → 91 formátumcsere után), `ovr_GetSessionStatus` hívás és Windows-hibaesemény nélkül |
| 2026-11-03 | Hibaok | A tálcamenü „Use OpenXR runtime” opciója alapból ki van kapcsolva, és újraindításkor elvész. Kikapcsolva az overlay OpenVR-en át indít, `/openxr` nélkül, így a ReviveXR be sem töltődik |
| 2026-11-03 | Újraindítás után | SteamVR fut, az OpenXR runtime a SteamVR. A First Contact és a Lone Echo telepítve van |
| 2026-11-03 | Devlog | Elkészült ez a dokumentum, számítógép-újraindítás a SteamVR miatt |
| 2026-11-03 | Commit `ed80a03` | A qmake által generált fájlok bekerültek a `.gitignore`-ba |
| 2026-11-03 | Commit `797be89` | ReviveXR SteamVR-javítások, overlay és injector módosítások, vcpkg |
| 2026-11-03 | Teszt | A heartbeat `HTTP 200`-at ad, a First Contact indítása sikertelen (nincs telepítve) |
