# Owner-controlled releases

This repository is maintained at
<https://github.com/williamlululu/transitink-os> and preserves the history and
attribution of <https://github.com/Zerie55699/transitink-os>.

## Update feed

Release builds use this default feed:

<https://williamlululu.github.io/transitink-os>

The device fetches `ota-manifest.json` over verified HTTPS, validates the
product, board, semantic version, image size and SHA-256 digest, then downloads
the versioned application image. The updater writes the inactive OTA
application partition and leaves NVS and LittleFS unchanged.

## One-time repository setup

1. Keep the repository public so GitHub Pages is available without a paid
   private-repository plan.
2. Enable GitHub Actions for the fork.
3. Under **Settings → Pages → Build and deployment**, select **GitHub Actions**.
4. Keep the workflow permissions declared in `.github/workflows/release.yml`.
   The workflow uses repository-relative release and Pages destinations.

## Publishing a release

1. Update `FIRMWARE_VERSION`, the changelog and release notes.
2. Run the generated-source checks, all tests, a clean `zectrix_note4` build
   and high-severity static analysis.
3. Generate and inspect the installer package with
   `scripts/package_installer.py --expected-version X.Y.Z`.
4. Commit the clean tree and create an annotated `vX.Y.Z` tag.
5. Push the commit, then the tag. The tag workflow rebuilds the release,
   publishes both application and merged images, and deploys the same verified
   package to GitHub Pages.
6. Before offering the update, fetch the live `ota-manifest.json` without a
   cache and verify its version, board, size and SHA-256 against the published
   application image.

## Transition from the upstream feed

TransitInk OS 1.2.0 is compiled to query the upstream GitHub Pages endpoint and
does not expose a local binary-upload route. Under the repository-supported
procedures, the one-time transition to 1.2.2 must use the merged ESP Web Tools
installation. Record the current portal settings first, perform the supervised
full installation, and then restore the same settings through first-run setup.
The Wi-Fi password must be re-entered because the portal never exposes it.

After 1.2.2 is installed, subsequent releases can use **Update and keep
settings** through the owner-controlled feed.
