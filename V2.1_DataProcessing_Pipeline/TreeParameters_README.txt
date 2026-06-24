TreeParameters.csv — README
================================================================================
Js5 Sap Flow Sensor V2.1 Processing Pipeline
CUNY Advanced Science Research Center | Reinmann Lab | Next Generation Sensor Lab
Last updated: May 2026
================================================================================

OVERVIEW
--------
TreeParameters.csv contains tree- and sensor-level parameters required for data
processing. Each row represents one sensor deployment (one sensor on one tree).
Keep this file in the root project folder alongside the R scripts. All scripts
in this pipeline read from it by column name — do not rename or reorder columns.

Populate this file before running any processing scripts. Columns marked
REQUIRED must be filled in for the pipeline to run. Columns marked RECOMMENDED
are used in specific scripts as noted. Columns marked OPTIONAL can be left blank
if unavailable, but downstream calculations that depend on them will be skipped
or flagged.


COLUMN DESCRIPTIONS
-------------------

sensorID  [REQUIRED — all scripts]
    The unique device ID assigned to the datalogger during provisioning.
    Must match the device ID recorded in the raw data file header exactly
    (e.g., SFS01, SFS10). Used to link raw data files to tree parameters.

treeID  [REQUIRED — all scripts]
    Your identifier for the tree the sensor is installed on. Can be any
    consistent label used in your study (e.g., a plot-level tree number).
    Does not need to follow any specific format but must be consistent across
    all files.

species  [RECOMMENDED — Script 04]
    Species code for the instrumented tree (e.g., QURU for Quercus rubra,
    ACRU for Acer rubrum). Used in Script 04 for sapwood area calculations.
    Leave blank if unknown, but note that Script 04 will not run for that tree.

DBH_cm  [RECOMMENDED — Script 04]
    Diameter at breast height in centimeters, measured at 1.3 m above ground.
    Required for sapwood area estimation via Bovard et al. (2005) allometry
    in Script 04.

BarkDepth_cm  [OPTIONAL]
    Bark thickness in centimeters measured at the probe installation site,
    before bark removal. Useful for confirming probe needle position relative
    to the cambium and for documentation purposes. Measure with calipers or
    a ruler at the time of installation.

SapwoodDepth_cm  [OPTIONAL — Script 04]
    Radial depth of functional sapwood in centimeters, measured from the
    cambium inward. Can be measured directly from an increment core or
    estimated from species-specific allometry. If left blank, Script 04 will
    estimate sapwood depth from DBH using Bovard et al. (2005) where species-
    specific coefficients are available.

    Note on increment cores vs. allometry: Taking a core provides a direct
    measurement and is preferable where feasible. If coring is not possible
    (e.g., to avoid additional wounding on instrumented trees), allometric
    estimation from DBH is a reasonable alternative. See Bovard et al. (2005)
    for species-specific equations.

SapwoodArea_cm2  [OPTIONAL — Script 04]
    Cross-sectional area of functional sapwood in cm², measured directly
    from an increment core taken at the probe installation height. If
    provided, this value is used directly in Script 04 for volumetric sap
    flux calculations and overrides the Bovard et al. (2005) allometric
    estimate. This is the preferred option when cores are available, as it
    eliminates uncertainty introduced by allometric estimation.

    If left blank, Script 04 will estimate sapwood area from DBH using the
    Bovard et al. (2005) allometry (requires B_o, B_i, and DBH_cm to be
    filled in). If SapwoodArea_cm2 is blank AND B_o/B_i are blank, Script
    04 will skip that tree with a message.

    Default: leave blank if not measured directly.

k  [REQUIRED — Script 02]
    Thermal diffusivity of sapwood (cm² s⁻¹). Used directly in the heat
    pulse velocity (HPV) calculation in Script 02 as:

	Vhrm (cm/hr) = (k / x) × ln(ΔTd / ΔTu) × 3600

    The default value of 0.0025 cm² s⁻¹ (2.5 × 10⁻³ cm² s⁻¹) is the standard
    approximation from Marshall (1958) and is appropriate when species-specific
    values are unavailable. Species-specific values can be calculated from wood 
    density and moisture content following Burgess et al. (2001) Equations 8–12, or
    taken from the literature. 

    Default: 0.0025

x  [REQUIRED — Script 02]
    Distance between the heater needle and each sensor needle, in centimeters.
    For the Js5 V2.1 with East 30 Sensors three-needle probe, this is fixed
    at 0.6 cm (6 mm). This value should not be changed unless a different
    probe configuration is used.

    Default: 0.6

d_wound  [RECOMMENDED — Script 02]
    Wound correction factor (dimensionless). Accounts for the low-conductivity
    wound zone created around probe needles during drilling, which causes the
    Heat Ratio Method to underestimate true sap flow velocity. Applied as a
    simple multiplier:

        Vhrm_corrected = Vhrm_raw * d_wound

    Derived from Burgess et al. (2001), Table 1A, which provides correction
    coefficients (B) for a -0.6, 0, +0.6 cm probe configuration with 1.3 mm
    diameter stainless steel probes — matching the Js5 V2.1 / East 30 Sensors
    configuration.

    The appropriate row is selected based on estimated wound radius. Wound
    radius = needle radius + radius of damaged tissue beyond the needle edge.
    For 1.3 mm diameter needles (radius = 0.065 cm) with clean, lubricated
    insertion, a wound radius of approximately 0.17 cm is a reasonable minimum
    estimate, corresponding to roughly 1 mm of damaged tissue beyond the needle
    edge.

    Recommended default: 1.7023 (B coefficient for 0.17 cm wound radius,
    Burgess et al. 2001 Table 1A)

    Users who wish to apply a more conservative (larger) wound correction can
    select a higher wound radius row from Table 1A of Burgess et al. (2001).
    Ideally, wound radius should be verified by examining a sample increment
    core taken adjacent to a probe hole to directly observe the extent of
    damaged tissue.

    If left blank, Script 02 will apply no wound correction (equivalent to
    d_wound = 1.0) and will note this in the output.

B_o  [OPTIONAL — Script 04]
    Species-specific allometric coefficient for sapwood area estimation from
    DBH, following Bovard et al. (2005):

        A_s (cm²) = B_o * DBH_cm - B_i

    Values for common northeastern US hardwood species are provided in
    Bovard et al. (2005), Table 1. Example values:
        QURU (red oak):  B_o = 3.24
        ACRU (red maple): B_o = 17.04

    Required for Script 04. Leave blank if not running Script 04 or if
    species-specific coefficients are unavailable.

B_i  [OPTIONAL — Script 04]
    Species-specific allometric intercept for the Bovard et al. (2005)
    sapwood area equation (see B_o above). Example values:
        QURU (red oak):  B_i = 10.24
        ACRU (red maple): B_i = 110.66

    Required for Script 04. Leave blank if not running Script 04 or if
    species-specific coefficients are unavailable.

Note on applicability: 
The allometric equations in Bovard et al. (2005) were developed from trees at the University of Michigan Biological Station. They may not be perfectly transferable to other sites or populations. Users deploying on different sites or species not listed in Bovard et al. (2005) should use site-specific allometries where available, or treat these values as approximations.

Note on minimum DBH for ACRU: 
The red maple equation (As = 17.04 × DBH − 110.66) will return negative sapwood area values for trees with DBH below approximately 6.5 cm. Do not apply this equation to small-diameter stems.


SCRIPT DEPENDENCIES SUMMARY
----------------------------
Script 01  ParseRawData.R       sensorID only
Script 02  ProcessHPV.R         sensorID, k, x, d_wound
Script 03  Plots.R              sensorID, treeID
Script 04  SapFlux.R            sensorID, treeID, species, DBH_cm,
                                SapwoodArea_cm2 (or B_o + B_i), depth_note (see script comments)


REFERENCES
----------
Bovard, B.D., Curtis, P.S., Vogel, C.S., Su, H.-B., and Schmid, H.P. (2005).
    Environmental controls on sap flow in a northern hardwood forest.
    Tree Physiology 25(1): 31-38.

Burgess, S.S.O., Adams, M.A., Turner, N.C., Beverly, C.R., Ong, C.K.,
    Khan, A.A.H., and Bleby, T.M. (2001). An improved heat pulse method to
    measure low and reverse rates of sap flow in woody plants.
    Tree Physiology 21(9): 589-598.

Marshall, D.C. (1958). Measurement of sap flow in conifers by heat transport.
    Plant Physiology 33(6): 385-396.
