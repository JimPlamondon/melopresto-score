# MeloPresto Score local macOS identity

MeloPresto Score is a local development application published by MeloPresto LLC, melopresto.com. Its bundle identifier is `com.melopresto.score`. Product metadata lives in `buildscripts/melopresto_identity.cmake`; the local product version is `0.0.0`. The inherited MuseScore engine and score-format versions remain unchanged. About identifies the product and the engine separately and retains upstream attribution. No Apple account, Developer ID, notarization, public release or distribution signing is part of this workflow.

The application uses its own Qt organization/application settings, application data, recent-file state, credentials directory and multi-process channel. Automatic and manual upstream application update checks are disabled. External MuseScore URL callbacks are not registered. Internal MuseScore namespaces, executable/build target names, MusicXML vocabulary, archive XML and cloud API contracts retain their existing identities.

## Native documents

`.meloscore` uses the existing native ZIP archive reader/writer, including score XML, styles, images, thumbnails, excerpts and audio/view settings. `.mscz` and `.mscx` remain supported. `.melosong` remains a different, structured timed-note format. This application does not claim to edit the other family formats merely because their names exist.

New local scores default to `.meloscore`; explicit existing score paths retain their suffix. Open/save/copy, command-line conversion, startup-score selection and template discovery recognize the new suffix. Backups retain `.meloscore~` in `.melopresto-backup`; recovery uses `.meloscore.autosave` and restores the original document path. Autosave files are internal recovery artifacts, not a separately advertised public document type.

The macOS bundle exports `com.melopresto.score.document` and `com.melopresto.score.backup`, with the document icon and the Owner handler rank. Inherited MuseScore declarations are imported and their handlers are Alternate. The preview helper source uses its own child bundle identifier and supports the native Score document type. It is included in the default local package and its built metadata is required by the package verifier. Finder has been observed launching the locally signed helper and displaying actual MeloPresto staff notation, including a Unicode/multi-dot filename. Malformed input is refused and a subsequent valid preview still works. The previous exclusion was based on a qlmanage command-line host crash, which did not establish that Finder failed. On this Mac, Finder is the live verification route; registration or standalone rendering alone is insufficient.

## Canonical artwork

`share/icons/melopresto/sources.json` records the canonical crescent source path, hash, palette and meaning. The crescent is the full Do-to-Do glyph without notation overlays. Its path is copied byte-for-byte into a checked source input; the generator applies uniform placement/scaling and rosewood, brass and bone colors. It does not implement musical geometry. The icon of the MeloPresto Jammer application is not kept here; the `melopresto` repository generates it (`tools/generate_jammer_app_icons.py`).

Run `python3 tools/melo/generate_app_icons.py --check` to verify the SVG masters. On macOS, regenerate raster and ICNS resources with `python3 tools/melo/generate_app_icons.py --render --node <node> --sharp-module <sharp-module>`. The renderer requires Node, sharp and iconutil; Python itself uses the standard library. Do not hand-edit the generated masters.

## Reversible upstream promotion policy

MeloPresto Score sets MELO_SCORE_SUPPRESS_UPSTREAM_PROMOTIONS=ON in buildscripts/melopresto_identity.cmake. All gates read muse::productPromotionsEnabled(); QML (Qt's interface description language) reads the same value through api.productPromotionsEnabled. The option is a build policy, so saved user preferences or a remotely downloaded campaign cannot override it. The original offer content, actions, URLs and assets remain in the source tree.

To restore the upstream promotion behavior, configure with -DMELO_SCORE_SUPPRESS_UPSTREAM_PROMOTIONS=OFF and rebuild/package. Removing the downstream identity configuration also restores the enabled fallback. This does not change the independent application identity or re-enable upstream application updates; those are separate product decisions. Run python tools/melo/check_product_policy.py to compile-check all three configurations without rebuilding the main application.

| Surface | Suppressed behavior |
|---|---|
| Welcome carousel | Startup eligibility, direct dialog registration and model population are gated, including the version-triggered welcome reset |
| MuseHub campaigns | Automatic/manual checks, cached-result presentation and direct release-dialog registration are gated; this includes remotely supplied offers such as Vienna Video Pro |
| First-launch setup | Sound-library acquisition, tutorial/catalog promotion and the early-exit MuseHub invitation are bypassed; theme selection remains |
| Home sound store and Learn catalog | Menu entries and restored/deep-linked section routing are gated; remote catalog requests are suppressed because their feeds mix tutorials with product offers |
| Mixer acquisition links | Get more sounds/effects entries and their direct action handlers are gated; installed resources remain listed and usable |
| Save-destination invitation | Ordinary saving defaults locally without showing the free-cloud-storage invitation; explicit Save to cloud remains available |
| Post-publication Audio.com invitation | The invitation, retained automatic cross-sharing preference, follow-up handler and direct dialog route are gated; explicit audio sharing remains available |

Normal score operations, installed sample libraries, soundfonts and effects, account access, explicit cloud saving, ordinary help and required installed-sampler maintenance remain intact. MuseSampler maintenance notices concern an installed playback dependency and are not product-acquisition offers. No preference store is reset. Future upstream offer surfaces must use the same policy; the audit for this source revision covers the paths above rather than promising coverage of unknown future code.

Native project_test verifies that retained Audio.com sharing preferences cannot enable the cross-promotion or automatic follow-up; its temporary test settings are rolled back without persistence. Native musesounds_test verifies that suppressed startup, campaign and storefront entry points return before consulting services or creating network requests, including direct/manual and cached-offer paths. Hosted checks compile the enabled, suppressed and upstream-fallback policy configurations. Final installed-app checks verify absence of the welcome promotion and catalog/mixer links while confirming that local save and playback remain usable.

## Local build and packaging

Reuse the configured native build and its dependency cache. Configure `CMAKE_INSTALL_PREFIX` to a staging directory inside the source checkout, such as `package-local`, and set `MUSESCORE_REVISION` to the source commit being built. Build `MuseScoreStudio`, `MuseScoreQuickLookPreviewExtension`, `engraving_tests`, `project_test` and `muse_update_test`. The internal executable remains `mscore`; the installed outer bundle is `MeloPresto Score.app`.

Run `python3 tools/melo/package_local_macos.py --build build.release --output <new-path>/MeloPresto\ Score.app --macdeployqt <Qt>/bin/macdeployqt`. The script builds translation resources, installs into the configured staging directory, copies to a new destination, deploys runtime libraries/QML, preserves upstream QML plugin isolation, signs nested code with local ad-hoc signatures, then validates the bundle. Existing output destinations are refused. Packaging builds the preview helper from the configured source before installation, refuses a missing staged helper, deploys its runtime dependencies and requires preview metadata validation. The helper needs linker header padding so deployment can relocate its libraries. No security settings or Apple credentials are changed.

Register the verified app with Launch Services after installing it in the user's Applications directory. Do not change stock MuseScore file defaults or modify its installation. If replacing an earlier local Score development build, verify its bundle identifier and preserve that build until the replacement passes launch checks.

## Verification

`python3 tools/melo/check_score_identity.py --app <app>` validates the built metadata and resources. `python3 tools/melo/check_meloscore_roundtrip.py --executable <executable> --fixture <score.mscx> --output <new-evidence-directory>` exercises real save/reopen, archive payload parity against equivalent `.mscz` operations, notation SVG parity, MIDI parity and malformed-input refusal. Add `--platform cocoa` for a packaged macOS app; the packaged app includes Cocoa, while the development Qt installation also offers offscreen rendering. The SVG comparison ignores only the exported filename title. Existing first-save style normalization is compared against the same MuseScore operation on the control file.

Native regression suites cover archive members, Unicode/multi-dot filenames, original recovery paths, backups and disabled manual/automatic upstream update checks. Run the broader engraving, project and update suites once after the focused checks pass. Preserve preexisting test-output files when running suites in a retained working directory. The hosted codestyle workflow additionally verifies canonical icon sources and centralized names; hosted native builds remain a deliberately dispatched workflow. Final owner acceptance covers icon appearance, launching both apps, the macOS Documents-folder privacy choice, normal Save/Save As, reopening via Finder, recovery and playback. The local startup scan uses the inherited user-selected Documents asset folders; a newly identified app receives its own macOS privacy prompt.

Windows/Linux packaging, Intel validation, additional family-file handlers, an update service, Spotlight indexing, settings import and distribution signing remain later work. Existing technical identifiers retained for upstream interoperability are intentional.

## Platform references

Apple distinguishes owned exported types from supported imported types in [Defining file and data types for your app](https://developer.apple.com/documentation/uniformtypeidentifiers/defining-file-and-data-types-for-your-app). Handler priority follows [LSHandlerRank](https://developer.apple.com/documentation/bundleresources/information-property-list/cfbundledocumenttypes/lshandlerrank). The UI reads product metadata through Qt's [Application properties](https://doc.qt.io/qt-6/qml-qtquick-application.html).

## Quick Look verification procedure

Use a disposable native .meloscore containing recognizable notation. In Finder, select it and press Space; wait for rendered score content, not the initial blank loading panel or the document icon. Confirm the title and notation and Open with MeloPresto Score action. Repeat with a Unicode/multi-dot filename, then malformed input, then a valid document again. Check the installed helper’s log for matching file paths, successful page dimensions and the expected malformed-input refusal. Compare file hashes before and after. Do not publish or change scores during these checks.

On the tested macOS 26.6.2 installation, the earlier qlmanage preview test threw a nil-key exception inside ExtensionFoundation before any observed renderer call, while Finder successfully hosted the local ad-hoc signed extension. Do not use that command-line exception alone to disable the feature or infer a requirement for Apple registration or notarization. [Apple describes the data-based preview contract](https://developer.apple.com/documentation/quicklookui/qlpreviewprovider); the actual installed Finder result is the acceptance evidence.

Run python -m unittest discover -s tools/melo/tests -p test_quicklook_packaging.py for the default-inclusion and missing-helper regressions. These mock external packaging commands to isolate package composition; they supplement, and do not replace, the real built-bundle and Finder checks. An explicit opt-in flag is no longer needed.
