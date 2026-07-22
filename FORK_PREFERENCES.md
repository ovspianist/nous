# Fork Preferences

This is a personal fork of [`unitreign/nous`](https://github.com/unitreign/nous). When resolving upstream conflicts, preserve the choices below unless the owner explicitly changes them. Accept unrelated upstream improvements, and prefer an upstream setting or implementation when it can preserve the same behavior cleanly.

## Interface and reading

- Use the legacy **Terminus** profile for UI text (menus, settings, and book lists). Keep **Inter** available behind the existing profile switch, but do not make it the default. This preference does not apply to book-reading fonts.
- Keep sleep wallpapers on **Auto Rotate** by default so successive sleeps cycle through available images; users may still pin one image.
- Keep Unitreign's configurable **Sleep Text** setting rather than bypassing its implementation. The owner's preferred setting is **Hide**.
- When the progress style is a bar, show both scopes: the selected chapter/book bar at the bottom and the complementary bar at the top.

## Controls

- Use **CW** and **CCW** terminology for rotation actions.
- Default awake power-button actions: tap = **Next Page**, hold = **Sleep**. Do not change the separate pre-boot hold-to-wake behavior.
- Default side-button holds: Up = **Rotate CCW**, Down = **Rotate CW**.
- Default bottom-button holds: Left = **Font Smaller**, Right = **Font Larger**; Back and OK remain unassigned but configurable.
- Default physical directions: side Top = **Previous Page**, bottom Right = **Next Page**, and menu Right = **Down**.
- Keep all tap/hold mappings configurable under **Settings → Control**.

## CrossInk interoperability

- Preserve recent-books and approximate reading-position exchange with CrossInk under `/.nous-crossink-reader-sync/`.
- Preserve sequenced deletion tombstones so removing a recent book in either firmware syncs without deleting the book or Nous reading-time data.
- Keep each firmware's native progress data authoritative; shared positions are an interoperability hint, not a replacement.
