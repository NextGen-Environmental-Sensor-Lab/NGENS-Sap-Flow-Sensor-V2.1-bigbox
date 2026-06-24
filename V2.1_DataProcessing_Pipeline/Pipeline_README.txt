Js5 Sap Flow Sensor V2.1 -- Data Processing Pipeline README
CUNY Advanced Science Research Center | Reinmann Lab | Next Generation Sensor Lab
Last updated: May 2026

Contact: Evonne Aguirre | CUNY Graduate Center | Reinmann Lab, CUNY ASRC
GitHub: https://github.com/NextGen-Environmental-Sensor-Lab/NGENS-Sap-Flow-Sensor-V2.1-bigbox

OVERVIEW
--------
This pipeline processes raw data files from the Js5 Sap Flow Sensor V2.1,
a low-cost, open-source heat pulse sap flow monitoring system developed at
the CUNY Advanced Science Research Center. The sensor measures sap flux --
the rate of water movement through a tree's xylem -- using the Heat Ratio
Method (HRM) with an East 30 Sensors three-needle probe array.

The pipeline takes raw .csv files from the datalogger, parses and quality-
checks them, calculates heat pulse velocity (HPV) at three sapwood depths,
and optionally scales HPV to volumetric sap flux using sapwood area.

The Js5 sensor design is based on Beslity et al. (2022) with hardware and
firmware modifications developed by the Next Generation Sensor Lab
(Ricardo Toledo-Crow) and Reinmann Lab at CUNY ASRC.


FOLDER STRUCTURE
----------------
Place all folders and scripts in one root project folder:

    ProjectFolder/
    |
    |-- TreeParameters.csv          Tree and sensor parameters (fill in first)
    |-- TreeParameters_README.txt   Column guide for TreeParameters.csv
    |-- Pipeline_README.txt         This file
    |
    |-- 01_ParseRawData.R           Script 1: Parse raw data files
    |-- 02_ProcessHPV.R             Script 2: Calculate heat pulse velocity
    |-- 03_Plots.R                  Script 3: Diagnostic plots
    |-- 04_SapFlux.R                Script 4: Volumetric sap flux (optional)
    |
    |-- 01_RawData_Archive/         Raw files archived by download date
    |   |-- 2026-05-12/
    |   |-- 2026-05-19/
    |   |-- ...
    |
    |-- 02_RawData_Current/         Current raw files (scripts read from here)
    |-- 03_CleanedData/             Output of Script 1 (auto-created)
    |-- 04_HPV/                     Output of Script 2 (auto-created)
    |-- 05_Plots/                   Output of Script 3 (auto-created)
    |-- 06_SapFlux/                 Output of Script 4 (auto-created, optional)


SCRIPT SUMMARY
--------------
Script 1 -- 01_ParseRawData.R
    Input  : Raw .csv files in 02_RawData_Current/
    Output : One cleaned .csv per sensor in 03_CleanedData/
             ParseSummary.csv (quality control summary)
    Does   : Parses the mixed metadata/data format of V2.1 raw files,
             assigns sequential pulse numbers, and applies four quality
             control flags per pulse (low voltage, short pulse, thermistor
             out of range, heater did not fire). Flagged pulses are retained
             in output -- not removed.

Script 2 -- 02_ProcessHPV.R
    Input  : Cleaned .csv files in 03_CleanedData/
             TreeParameters.csv (k, x, d_wound)
    Output : One HPV .csv per sensor in 04_HPV/
             HPV_Summary.csv
    Does   : Calculates heat pulse velocity (cm/hr) for each pulse at three
             sapwood depths (outer, middle, inner) using the Heat Ratio
             Method (Burgess et al. 2001). Applies wound correction factor.
             Outputs both raw and wound-corrected HPV.

Script 3 -- 03_Plots.R
    Input  : Cleaned .csv files in 03_CleanedData/ (Sections 1 and 3)
             HPV .csv files in 04_HPV/ (Section 2)
    Output : PDF plots in 05_Plots/
    Does   : Three plot types:
             Section 1 -- Battery voltage over time (one PDF per sensor)
             Section 2 -- HPV time series, all three depths (one PDF per sensor)
             Section 3 -- Per-pulse temperature plots (commented out by default;
                          slow for full season data -- see script for instructions)

Script 4 -- 04_SapFlux.R  [OPTIONAL]
    Input  : HPV .csv files in 04_HPV/
             TreeParameters.csv (SapwoodArea_cm2 or B_o + B_i + DBH_cm)
    Output : One sap flux .csv per sensor in 06_SapFlux/
             SapFlux_Summary.csv
    Does   : Scales wound-corrected HPV to volumetric sap flux (cm3/hr)
             using sapwood area. Outputs sap flux for all three thermistor
             depths separately. See script header for important notes on
             depth selection by wood anatomy type.


WORKFLOW
--------
FIRST TIME SETUP

    1. Fill in TreeParameters.csv with one row per sensor deployment.
       See TreeParameters_README.txt for column descriptions and default
       values. At minimum, fill in sensorID, treeID, k, x, and d_wound
       before running Scripts 1 and 2.

    2. Open each script and update root_dir at the top:
           root_dir <- "path/to/your/project/folder"

    3. Install required R packages if not already installed:
           install.packages(c("tidyverse", "lubridate"))

RUNNING THE PIPELINE

    Step 1 -- Parse raw data
        Place raw sensor .csv files in 02_RawData_Current/
        Run 01_ParseRawData.R
        Check 03_CleanedData/ParseSummary.csv for flagged pulses

    Step 2 -- Calculate HPV
        Run 02_ProcessHPV.R
        Check 04_HPV/HPV_Summary.csv for processing summary

    Step 3 -- Make diagnostic plots
        Run 03_Plots.R
        Review PDFs in 05_Plots/ to check data quality

    Step 4 -- Calculate volumetric sap flux [optional]
        Fill in sapwood area parameters in TreeParameters.csv
        Run 04_SapFlux.R
        See 06_SapFlux/ for output

WEEKLY DATA DOWNLOAD PROCEDURE

    Each weekly download produces a cumulative file containing all data
    from the start of the season to the download date.

        1. Download all data files from sensors in the field
        2. Copy downloaded files into 01_RawData_Archive/YYYY-MM-DD/
           (create a new subfolder with today's date -- never modify these)
        3. Copy the same files into 02_RawData_Current/
           (replace the previous week's files)
        4. Re-run Scripts 1 through 3

    The archive folder ensures you always have an untouched copy of every
    download. If a file in 02_RawData_Current/ is ever corrupted or
    accidentally modified, restore it from the archive.


SOFTWARE REQUIREMENTS
---------------------
R version 4.0 or higher (https://www.r-project.org/)

Required packages: tidyverse, lubridate

Install with:
    install.packages(c("tidyverse", "lubridate"))


QUALITY CONTROL FLAGS
---------------------
Script 1 assigns four flags per pulse. Flagged pulses are retained in all
output files -- they are not automatically removed. Review flagged pulses
before drawing conclusions from the data.

    LowVoltage_flag
        Battery voltage below threshold (default 10.5V). May indicate
        battery pack needs replacing. Check BatteryVoltage plots in 05_Plots/.

    ShortPulse_flag
        Fewer rows recorded than expected based on sensor timing parameters.
        Pulse may be incomplete. HPV calculation may be unreliable.

    ThermOutOfRange_flag
        One or more thermistor readings outside 0-50 degC. May indicate
        sensor malfunction or loose connection. Review per-pulse plots
        (Section 3 of Script 3).

    HeaterDidNotFire_flag
        No heater current detected during heat phase. HPV cannot be
        calculated without a heat pulse. Exclude these pulses from analysis.


NOTES ON DATA INTERPRETATION
-----------------------------
Wounding response:
    Drilling probe holes causes a wounding response in the tree that can
    affect sap flow readings for 1-2 weeks after installation, varying by
    species and time of year. Exclude the first 1-2 weeks of data after
    installation from analysis.

Depth selection for analysis:
    HPV and sap flux are output for three thermistor depths (outer 5 mm,
    middle 17.5 mm, inner 30 mm). Which depth(s) to use depends on wood
    anatomy:
    - Ring-porous species (oaks, ashes): use outer depth only
    - Diffuse-porous species (maples, birches): consider all depths
    See Script 4 header and Burgess et al. (2001) for guidance.

Negative HPV values:
    Small negative values can occur at low flow rates due to measurement
    noise and are not automatically removed. They may also reflect genuine
    reverse flow. Zero-flooring decisions should be made at the analysis
    stage with justification.


REFERENCES
----------
Beslity, J., Shaw, S.B., Drake, J.E., Fridley, J., Stella, J.C., Stark, J.,
    and Singh, K. (2022). A low cost, low power sap flux device for
    distributed and intensive monitoring of tree transpiration. HardwareX
    12: e00351. https://doi.org/10.1016/j.ohx.2022.e00351

Bovard, B.D., Curtis, P.S., Vogel, C.S., Su, H.-B., and Schmid, H.P. (2005).
    Environmental controls on sap flow in a northern hardwood forest.
    Tree Physiology 25(1): 31-38.

Burgess, S.S.O., Adams, M.A., Turner, N.C., Beverly, C.R., Ong, C.K.,
    Khan, A.A.H., and Bleby, T.M. (2001). An improved heat pulse method
    to measure low and reverse rates of sap flow in woody plants.
    Tree Physiology 21(9): 589-598.

Marshall, D.C. (1958). Measurement of sap flow in conifers by heat transport.
    Plant Physiology 33(6): 385-396.
