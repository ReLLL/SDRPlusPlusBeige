# Band shortcuts

Read when: changing the top band selector, preset ranges, colors or tooltips.

The selector before the band buttons offers HAM, Broadcast and Utility. HAM is the default; an explicit choice persists as `bandShortcutCategory`. Missing, unknown or non-string preferences fall back to HAM. Changing category only changes the shortcuts: frequency, modulation, tuning mode and IQ replay remain as they were. Clicking a band centers its range through the existing tuning operation, preserving free/center mode. Modulation remains a separate Radio choice.

HAM keeps all 16 chart-based ranges, colors and tooltip fields. Broadcast uses a copper-to-amber palette; Utility uses steel blue to teal. Text contrast follows the fill for the new categories. The selector reserves a fixed width so its label does not shift neighboring buttons. Buttons wrap into two rows beside the tuning controls, or move below them when necessary. The SNR meter and logo keep their space. Catalogs and layout snapshots use fixed storage; drawing does not construct JSON or resize containers.

## Broadcast

Seventeen shortcuts: LW, MW, 120m, 90m, 75m, 60m, 49m, 41m, 31m, 25m, 22m, 19m, 16m, 15m, 13m, 11m and FM. LW/MW and 41m use Region 1 ranges appropriate to the current location. Tropical bands are international listening targets, not an assertion of local broadcast activity. The 60m search window includes the 4.995-5.005 MHz time-signal gap; its tooltip says so. FM covers 87.5-108 MHz. Presets do not change the selected band-plan overlay or receiver span.

Ranges follow the [ITU terrestrial broadcasting table](https://www.itu.int/en/ITU-R/terrestrial/broadcast/Pages/bands.aspx), checked October 23, 2026. This is receive navigation, independent of national transmit permissions. Time and coverage descriptions are broad propagation guidance, not station schedules or measured averages.

## Utility

Sixteen shortcuts:

| Button | Range (MHz) | Search target |
| --- | --- | --- |
| Air 3 | 2.850-3.025 | HF route aviation |
| Sea 4 | 4.000-4.063 | Maritime simplex/shared fixed service |
| Air 5 | 5.480-5.680 | HF route aviation |
| Air 6 | 6.525-6.685 | HF route aviation |
| Sea 8 | 8.100-8.195 | Maritime simplex/shared fixed service |
| Air 8 | 8.815-8.965 | HF route aviation |
| Gov HF | 11.175-11.275 | Off-route aviation, including government/military users |
| Air 11 | 11.275-11.400 | HF route aviation |
| Air 13 | 13.260-13.360 | HF route aviation |
| Air 18 | 17.900-17.970 | HF route aviation |
| CB | 26.960-27.410 | Citizens band voice |
| Nav | 108.000-117.975 | VOR/ILS and navigation/data |
| Air VHF | 117.975-137.000 | Civil airband voice/data |
| Marine | 156.025-162.025 | International maritime VHF window |
| Mil Air | 225.000-400.000 | UHF military aviation window |
| PMR | 446.000-446.200 | PMR446 handheld voice/data |

Gov HF is an off-route aeronautical window, not a universal or exclusive government allocation. Mil Air includes navigation, satellite and other services; Marine includes gaps, simplex and duplex channels. A band's midpoint can be quiet or between channels. Select a signal within the band and choose its mode in Radio; the toolbar does not scan or decode services by itself. New tooltips use Best time to listen, Typical use and Typical coverage, without displaying center frequency.

Primary sources, checked October 23, 2026:

- [ICAO Doc 9718, Volume I, advance third edition](https://www.icao.int/sites/default/files/FSMP/Doc.9718-Vol-I_3rd-Edition-2023-DRAFT.pdf): HF route segments, VHF communications and navigation. This public edition is marked draft.
- [ComReg frequency table](https://www.comreg.ie/media/dlm_uploads/2015/12/odtr0123.pdf): off-route HF and government service context; an allocation reference, not a current station list.
- [USCG HF simplex channels](https://www.navcen.uscg.gov/simplex) and [international VHF maritime channels](https://navcen.uscg.gov/international-vhf-marine-radio-channels-freq): marine windows and channel structure.
- [FAA frequency coordination bands](https://webfcr.faa.gov/): UHF aviation coordination range and its navigation exclusion. The broad preset is a search window, not an exclusive voice allocation.
- [ISED RSS-236](https://ised-isde.canada.ca/site/spectrum-management-telecommunications/en/devices-and-equipment/radio-equipment-standards/radio-standards-specifications-rss/rss-236-general-radio-service-equipment-operating-band-26960-27410-mhz-citizens-band): CB band and carrier table.
- [CEPT ECC Decision (15)05](https://docdb.cept.org/download/2783): PMR446 range.

## Checks

`beige_band_presets` checks catalogs, category parsing, distinct palettes and fitting odd/even button counts into two rows. Run `beige/tests/band_categories_smoke.py --url URL` against an isolated QA profile; repeat with `--center-tuning` in a centered profile. It checks all 49 shortcuts, category-only stability, invalid requests and rendered rectangle separation. Keep running the original `ham_bands_smoke.py` in both tuning modes.

QA-only endpoints: `/proc/display/band-category`, `/proc/display/band`, `/proc/display/band-catalog` and `/proc/display/band-layout`. Existing HAM endpoints remain compatible. HTTP stays disabled on normal launches.
