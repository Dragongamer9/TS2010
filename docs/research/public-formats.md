# What is publicly known about TS2009/TS2010 file formats and tools

Research by Perplexity (Command Center teammate), consulted 2026-11-11; saved and summarised by
Claude. Sources keep their stated scope: the official Content Creator's Guide (CCG) covers data
"up to TRS2009-SP1"; pages on online.ts2009.com can include much later changes, so the host
name alone does not make a page TS2009/TS2010 documentation. Public documentation describes
**authoring and configuration**; it does not give the binary layouts this project decoded from
the disc itself. Nothing here changes the project's own "checked on the disc" or "model" labels.

| Topic | Publicly documented | Not documented publicly (found) | Sources |
|---|---|---|---|
| `.ja` archives | that "Jet Archive" files hold world content (2006, TRS2004) | header, index, compression (decoded here from the disc and the DLL) | [forum 2006](https://forums.auran.com/threads/how-to-modify-the-jet-archive-files.866/); TZarchiver handles `.tzarc`, a different format ([GitHub](https://github.com/SilverGreen93/TZarchiver)) |
| `.im` / `.pm` meshes | roles (Indexed vs older Progressive mesh); the official importer converts an XML format into the "proprietary" binary (obsolete from trainz-build 4.5) | the binary chunk layout (decoded here) | [Trainz Mesh Importer](https://wiki.trainzsimulator.com/wiki/Trainz_Mesh_Importer); a community `.im`/`.kin` **writer**: [BlenderIMExporter](https://github.com/rileyzzz/BlenderIMExporter); PEV's PM2IM and Mesh Viewer ([PEV tools](https://online.ts2009.com/mediaWiki/index.php/Trainz_Tools_by_PEV)), source not public |
| `.kin` | authoring via the importer's XML | binary layout (decoded here) | as above |
| `.texture`, `.texture.txt` | the descriptor and its options; internal formats incl. DXT | the compiled layout (decoded here) | [Texture file](https://wiki.trainzsimulator.com/wiki/Texture_file); a GPL-3.0 reader in progress: [TrainzTexture](https://github.com/rileyzzz/TrainzTexture) |
| `mapfile.gnd` / `.obs` / `.trk` | their roles (ground, placed objects, track) | record layouts, curve rules (decoded / modelled here) | [CCG/Example:Map](https://online.ts2009.com/mediaWiki/index.php/CCG/Example:Map); [Wikibooks file types](https://en.wikibooks.org/wiki/Trainz/file_types_by_extensions) |
| `profile.dat` | it is the session, made by Surveyor, not human-readable | the binary layout (decoded here) | [CCG/Example:Profile](https://wiki.trainzsimulator.com/wiki/CCG/Example:Profile); [forum 2009](https://forums.auran.com/threads/reading-profile-dat-files.40512/) |
| `enginespec`, traincar physics | many keys: `mass`; `motor` (`maxspeed` m/s is DCC mode), `resistance`, `max-accel`/`max-decel` (DCC); `throttle-power` (speed m/s → tractive effort **kN** per notch, cab mode); brake containers `pressure`, `flowsize`, `volume` | the game's actual solver | [CCG/Engine](https://online.ts2009.com/mediaWiki/index.php/CCG/Engine), [Throttle-power](https://online.ts2009.com/mediaWiki/index.php/%22Throttle-power%22_container), [Motor](https://online.ts2009.com/mediaWiki/index.php/%22Motor%22_container) |
| Soundscripts, enginesound | soundscript keys (`repeat-delay`, `distance`, `trigger`, `volume`, attachments); enginesound behaviour (diesel transitions, electric pitch, steam chuffs) | exact mixing and attenuation | [Soundscript container](https://online.ts2009.com/mediaWiki/index.php/%22Soundscript%22_container), [KIND Enginesound](https://online.ts2009.com/mediaWiki/index.php/KIND_Enginesound) |
| TrainzScript `.gs` / `.gsl` / `.gse` | `.gs` is plain-text source; `.gsl` a compiled library (TRS2004 guide); TS2009 has a global and an active-module VM; the class library API | bytecode, VM, `.gse` encryption | [Getting Started](https://online.ts2009.com/mediaWiki/index.php/Getting_Started_in_TrainzScript), [TS2009 features](https://online.ts2009.com/mediaWiki/index.php/New_TrainzScript_Features_-TS2009), [Library Reference](https://online.ts2009.com/mediaWiki/index.php/TrainzScript_Library_Reference); an unverified `.gse` tool: [TrainzScriptGseTool](https://github.com/DF11G2001/TrainzScriptGseTool) (not inspected) |
| CDP packages | extraction, chump and binary-KUID notes | (not this project's problem: the install uses `.ja`) | [CDPExplorer](https://github.com/SilverGreen93/CDPExplorer) |

**Most useful next:** the `enginespec` documentation, for driving trains with their own
physics. The `throttle-power` and `dynamic-brake` curves are **force in kN** against speed in
m/s, not acceleration. Keep DCC keys (`motor.maxspeed`, `max-accel`) apart from cab-mode keys.
Keys documented only after TS2010 (e.g. `full-throttle-speed`, `driving-wheel-weight-ratio`)
are not assumed. (The tail of the research report, on `.gse` and open-source readers, was cut
off in transfer and is not included here.)
