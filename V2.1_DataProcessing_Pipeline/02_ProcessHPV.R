# =============================================================================
# 02_ProcessHPV.R
# Js5 Sap Flow Sensor V2.1 Processing Pipeline
# CUNY Advanced Science Research Center | Reinmann Lab | Next Generation Sensor Lab
#
# PURPOSE:
#   Reads cleaned sensor files from 03_CleanedData/, calculates heat pulse
#   velocity (HPV) for each pulse at three sapwood depths using the Heat
#   Ratio Method (HRM), applies the wound correction factor, and writes
#   one processed .csv per sensor to 04_HPV/.
#
# METHOD:
#   Heat Ratio Method (HRM) following Burgess et al. (2001):
#
#     Vhrm (cm/hr) = (k / x) * ln(ΔTd / ΔTu) * 3600
#
#   Where:
#     k    = thermal diffusivity of sapwood (cm² s⁻¹)
#     x    = needle spacing, heater to sensor (cm)
#     ΔTd  = mean downstream temp in post-heat window (60-100s) minus
#             mean pre-heat baseline
#     ΔTu  = mean upstream temp in post-heat window (60-100s) minus
#             mean pre-heat baseline
#
#   Thermistor pairs by depth:
#     Outer  : upstream = NO, downstream = FO  (5 mm from cambium)
#     Middle : upstream = NM, downstream = FM  (17.5 mm from cambium)
#     Inner  : upstream = NI, downstream = FI  (30 mm from cambium)
#
#   Wound correction (Burgess et al. 2001, Table 1A):
#     Vhrm_corrected = Vhrm * d_wound
#
# INPUT:
#   - Cleaned .csv files in 03_CleanedData/ (output of 01_ParseRawData.R)
#   - TreeParameters.csv in root project folder
#
# OUTPUT:
#   - One processed .csv per sensor in 04_HPV/
#   - A summary log: 04_HPV/HPV_Summary.csv
#
# COLUMN GUIDE FOR OUTPUT FILES:
#   SensorID          : Device ID
#   treeID            : Tree identifier from TreeParameters
#   Pulse_DateTime    : Timestamp of first post-heat row for this pulse
#   PulseNum          : Sequential pulse number
#   Pulse_PreHeatRows : Number of pre-heat rows recorded
#   Pulse_HeatRows    : Number of heat rows recorded
#   Pulse_PostHeatRows: Number of post-heat rows recorded
#   Pulse_AvgVoltage  : Mean battery voltage across the pulse
#   alpha_outer       : ln(ΔTd/ΔTu) for outer thermistor pair
#   alpha_middle      : ln(ΔTd/ΔTu) for middle thermistor pair
#   alpha_inner       : ln(ΔTd/ΔTu) for inner thermistor pair
#   Vhrm_outer        : Uncorrected HPV, outer depth (cm/hr)
#   Vhrm_middle       : Uncorrected HPV, middle depth (cm/hr)
#   Vhrm_inner        : Uncorrected HPV, inner depth (cm/hr)
#   Vhrm_outer_corr   : Wound-corrected HPV, outer depth (cm/hr)
#   Vhrm_middle_corr  : Wound-corrected HPV, middle depth (cm/hr)
#   Vhrm_inner_corr   : Wound-corrected HPV, inner depth (cm/hr)
#   LowVoltage_flag   : 1 if pulse was flagged for low voltage in Script 01
#   ShortPulse_flag   : 1 if pulse was flagged as short in Script 01
#   ThermOutOfRange_flag    : 1 if thermistor out of range flag from Script 01
#   HeaterDidNotFire_flag   : 1 if heater did not fire flag from Script 01
#
# NOTES:
#   - Flagged pulses are retained in output but should be interpreted with
#     caution. Consider excluding pulses where HeaterDidNotFire_flag = 1
#     from any analysis, as HPV cannot be calculated without a heat pulse.
#   - Vhrm values are NOT zero-floored here. Negative values can result from
#     noise at low flow rates and are physiologically possible at night
#     (reverse flow). Zero-flooring decisions should be made at the
#     analysis stage with justification.
#   - If d_wound is blank in TreeParameters, no wound correction is applied
#     and Vhrm_corr columns will equal Vhrm columns. This will be noted in
#     the summary log.
#   - This script outputs HPV (cm/hr) only. Scaling to volumetric sap flux
#     requires sapwood area and is handled in the optional Script 04.
#
# REFERENCES:
#   Burgess, S.S.O., Adams, M.A., Turner, N.C., Beverly, C.R., Ong, C.K.,
#     Khan, A.A.H., and Bleby, T.M. (2001). An improved heat pulse method
#     to measure low and reverse rates of sap flow in woody plants.
#     Tree Physiology 21(9): 589-598.
#   Marshall, D.C. (1958). Measurement of sap flow in conifers by heat
#     transport. Plant Physiology 33(6): 385-396.
# =============================================================================

library(tidyverse)
library(lubridate)

# =============================================================================
# PATHS — UPDATE THESE FOR YOUR SYSTEM
# =============================================================================
root_dir     <- "C:/Users/eviea/Graduate Center Dropbox/Evonne Aguirre/NSF_HF_Shared_Project_Folder/CLIFF_Data/Trees/Sap Flow/SF protocols/V2.1 Code Pipeline"   # <-- UPDATE THIS
cleaned_dir  <- file.path(root_dir, "03_CleanedData")
hpv_dir      <- file.path(root_dir, "04_HPV")
params_path  <- file.path(root_dir, "TreeParameters.csv")

dir.create(hpv_dir, showWarnings = FALSE)

# =============================================================================
# PARAMETERS
# =============================================================================
# Post-heat window used for ΔT calculation (seconds after heat pulse ends)
# Following Burgess et al. (2001): 60-100 seconds post-heat
POSTHEAT_WIN_START <- 60
POSTHEAT_WIN_END   <- 100

# =============================================================================
# LOAD AND VALIDATE TREE PARAMETERS
# =============================================================================
if(!file.exists(params_path)) {
  stop(
    "\nTreeParameters.csv not found. Expected it here:\n  ", params_path,
    "\nCheck that:\n",
    "  1. Your root_dir path is correct\n",
    "  2. TreeParameters.csv is saved in the root project folder\n",
    "  3. The filename is spelled exactly 'TreeParameters.csv'"
  )
}

params <- read_csv(params_path, show_col_types = FALSE)

# Check required columns are present
required_cols <- c("sensorID", "treeID", "k", "x")
missing_cols  <- required_cols[!required_cols %in% names(params)]

if(length(missing_cols) > 0) {
  stop(
    "\nTreeParameters.csv is missing required column(s): ",
    paste(missing_cols, collapse = ", "),
    "\nRequired columns for Script 02: ", paste(required_cols, collapse = ", "),
    "\nPlease check your TreeParameters.csv and try again."
  )
}

# =============================================================================
# CHECK FOR CLEANED FILES
# =============================================================================
cleaned_files <- list.files(cleaned_dir, pattern = "_cleaned\\.csv$",
                             full.names = TRUE, ignore.case = TRUE)

if(length(cleaned_files) == 0) {
  stop(
    "\nNo cleaned data files found in:\n  ", cleaned_dir,
    "\nExpected files ending in '_cleaned.csv'.",
    "\nMake sure you have run 01_ParseRawData.R first."
  )
}

# =============================================================================
# HELPER FUNCTION: Calculate HPV for one pulse
# =============================================================================
calc_hpv_pulse <- function(pulse_df, k, x) {
  
  # Separate phases
  preheat  <- pulse_df %>% filter(trimws(Flag) == "pre-heat")
  heat     <- pulse_df %>% filter(trimws(Flag) == "heat")
  postheat <- pulse_df %>% filter(trimws(Flag) == "post-heat")
  
  # Need at least some pre-heat and post-heat rows to calculate
  if(nrow(preheat) == 0 | nrow(postheat) == 0) {
    return(NULL)
  }
  
  # Add elapsed time within post-heat phase (seconds from heat pulse end)
  postheat <- postheat %>%
    mutate(ElapsedTime = seq(0, n() - 1, 1))
  
  # Subset post-heat to the 60-100 second window
  postheat_window <- postheat %>%
    filter(ElapsedTime >= POSTHEAT_WIN_START & ElapsedTime <= POSTHEAT_WIN_END)
  
  if(nrow(postheat_window) == 0) {
    warning(paste("Post-heat window (60-100s) empty for pulse",
                  unique(pulse_df$PulseNum),
                  "- pulse may be too short. Check ShortPulse_flag."))
    return(NULL)
  }
  
  # -------------------------------------------------------------------------
  # Calculate ΔT for each thermistor pair
  # ΔT = mean temp in post-heat window minus mean pre-heat baseline
  # -------------------------------------------------------------------------
  
  # Pre-heat baselines
  baseline_NO <- mean(preheat$NO, na.rm = TRUE)
  baseline_NM <- mean(preheat$NM, na.rm = TRUE)
  baseline_NI <- mean(preheat$NI, na.rm = TRUE)
  baseline_FO <- mean(preheat$FO, na.rm = TRUE)
  baseline_FM <- mean(preheat$FM, na.rm = TRUE)
  baseline_FI <- mean(preheat$FI, na.rm = TRUE)
  
  # Delta T: post-heat window mean minus pre-heat baseline
  dTu_outer  <- mean(postheat_window$NO, na.rm = TRUE) - baseline_NO
  dTd_outer  <- mean(postheat_window$FO, na.rm = TRUE) - baseline_FO
  
  dTu_middle <- mean(postheat_window$NM, na.rm = TRUE) - baseline_NM
  dTd_middle <- mean(postheat_window$FM, na.rm = TRUE) - baseline_FM
  
  dTu_inner  <- mean(postheat_window$NI, na.rm = TRUE) - baseline_NI
  dTd_inner  <- mean(postheat_window$FI, na.rm = TRUE) - baseline_FI
  
  # -------------------------------------------------------------------------
  # Calculate alpha = ln(ΔTd / ΔTu) for each depth
  # -------------------------------------------------------------------------
  alpha_outer  <- log(dTd_outer  / dTu_outer)
  alpha_middle <- log(dTd_middle / dTu_middle)
  alpha_inner  <- log(dTd_inner  / dTu_inner)
  
  # -------------------------------------------------------------------------
  # Calculate Vhrm = (k / x) * alpha * 3600
  # Units: k in cm²/s, x in cm, output in cm/hr
  # -------------------------------------------------------------------------
  Vhrm_outer  <- (k / x) * alpha_outer  * 3600
  Vhrm_middle <- (k / x) * alpha_middle * 3600
  Vhrm_inner  <- (k / x) * alpha_inner  * 3600
  
  # -------------------------------------------------------------------------
  # Pulse metadata
  # -------------------------------------------------------------------------
  pulse_datetime   <- min(postheat$DateTime, na.rm = TRUE)
  preheat_rows     <- nrow(preheat)
  heat_rows        <- nrow(heat)
  postheat_rows    <- nrow(postheat)
  avg_voltage      <- mean(pulse_df$Voltage, na.rm = TRUE)
  
  # Carry forward QC flags (take first value — constant per pulse)
  lv_flag    <- first(pulse_df$LowVoltage_flag)
  sp_flag    <- first(pulse_df$ShortPulse_flag)
  tor_flag   <- first(pulse_df$ThermOutOfRange_flag)
  hnf_flag   <- first(pulse_df$HeaterDidNotFire_flag)
  
  return(data.frame(
    Pulse_DateTime     = pulse_datetime,
    Pulse_PreHeatRows  = preheat_rows,
    Pulse_HeatRows     = heat_rows,
    Pulse_PostHeatRows = postheat_rows,
    Pulse_AvgVoltage   = round(avg_voltage, 4),
    alpha_outer        = round(alpha_outer,  6),
    alpha_middle       = round(alpha_middle, 6),
    alpha_inner        = round(alpha_inner,  6),
    Vhrm_outer         = round(Vhrm_outer,  4),
    Vhrm_middle        = round(Vhrm_middle, 4),
    Vhrm_inner         = round(Vhrm_inner,  4),
    LowVoltage_flag         = lv_flag,
    ShortPulse_flag         = sp_flag,
    ThermOutOfRange_flag    = tor_flag,
    HeaterDidNotFire_flag   = hnf_flag
  ))
}

# =============================================================================
# MAIN LOOP — process each cleaned sensor file
# =============================================================================
summary_log <- list()

for(f in cleaned_files) {
  
  # Extract sensor ID from filename
  sensor_id <- sub("_cleaned\\.csv$", "", basename(f), ignore.case = TRUE)
  message(paste("Processing:", sensor_id))
  
  # -------------------------------------------------------------------------
  # Match to TreeParameters
  # -------------------------------------------------------------------------
  tree_params <- params %>% filter(sensorID == sensor_id)
  
  if(nrow(tree_params) == 0) {
    message(sprintf(
      "  Skipping %s — no matching row found in TreeParameters.csv.\n  Check that sensorID '%s' is spelled correctly in TreeParameters.csv.",
      sensor_id, sensor_id
    ))
    next
  }
  
  if(nrow(tree_params) > 1) {
    message(sprintf(
      "  Warning: multiple rows found for sensorID '%s' in TreeParameters.csv. Using first row.",
      sensor_id
    ))
    tree_params <- tree_params[1, ]
  }
  
  # Extract parameters
  tree_id <- tree_params$treeID
  k       <- tree_params$k
  x       <- tree_params$x
  d_wound <- tree_params$d_wound
  
  # Validate k and x
  if(is.na(k) | k <= 0) {
    message(sprintf(
      "  Skipping %s — k value in TreeParameters is missing or zero.\n  k must be a positive number (default: 0.0025 cm² s⁻¹).",
      sensor_id
    ))
    next
  }
  
  if(is.na(x) | x <= 0) {
    message(sprintf(
      "  Skipping %s — x value in TreeParameters is missing or zero.\n  x must be a positive number (default: 0.6 cm for Js5 V2.1).",
      sensor_id
    ))
    next
  }
  
  # Handle missing wound correction
  wound_correction_applied <- TRUE
  if(is.na(d_wound)) {
    message(sprintf(
      "  Note: d_wound is blank for %s. No wound correction will be applied (d_wound = 1.0).\n  To apply wound correction, add d_wound value to TreeParameters.csv.",
      sensor_id
    ))
    d_wound <- 1.0
    wound_correction_applied <- FALSE
  }
  
  # -------------------------------------------------------------------------
  # Read cleaned data
  # -------------------------------------------------------------------------
  alldata <- tryCatch({
    read_csv(f, show_col_types = FALSE)
  }, error = function(e) {
    message(sprintf("  Skipping %s — could not read file: %s", sensor_id, e$message))
    return(NULL)
  })
  
  if(is.null(alldata)) next
  
  # Sort by datetime
  alldata <- alldata %>%
    mutate(DateTime = as.POSIXct(DateTime, format = "%Y-%m-%d %H:%M:%S", tz = "UTC")) %>%
    arrange(DateTime)
  
  # -------------------------------------------------------------------------
  # Loop over pulses and calculate HPV
  # -------------------------------------------------------------------------
  pulse_list  <- list()
  pulse_nums  <- sort(unique(alldata$PulseNum))
  n_skipped   <- 0
  
  for(pnum in pulse_nums) {
    
    pulse_df <- alldata %>% filter(PulseNum == pnum)
    
    result <- calc_hpv_pulse(pulse_df, k, x)
    
    if(is.null(result)) {
      n_skipped <- n_skipped + 1
      next
    }
    
    result$PulseNum <- pnum
    pulse_list[[length(pulse_list) + 1]] <- result
  }
  
  if(length(pulse_list) == 0) {
    message(sprintf("  No valid pulses processed for %s. Check your data.", sensor_id))
    next
  }
  
  # -------------------------------------------------------------------------
  # Combine pulse results
  # -------------------------------------------------------------------------
  results_df <- bind_rows(pulse_list)
  
  # Apply wound correction
  results_df <- results_df %>%
    mutate(
      Vhrm_outer_corr  = round(Vhrm_outer  * d_wound, 4),
      Vhrm_middle_corr = round(Vhrm_middle * d_wound, 4),
      Vhrm_inner_corr  = round(Vhrm_inner  * d_wound, 4)
    )
  
  # Add sensor and tree ID, reorder columns
  results_df <- results_df %>%
    mutate(SensorID = sensor_id, treeID = tree_id) %>%
    select(SensorID, treeID, Pulse_DateTime, PulseNum,
           Pulse_PreHeatRows, Pulse_HeatRows, Pulse_PostHeatRows,
           Pulse_AvgVoltage,
           alpha_outer, alpha_middle, alpha_inner,
           Vhrm_outer, Vhrm_middle, Vhrm_inner,
           Vhrm_outer_corr, Vhrm_middle_corr, Vhrm_inner_corr,
           LowVoltage_flag, ShortPulse_flag,
           ThermOutOfRange_flag, HeaterDidNotFire_flag)
  
  # -------------------------------------------------------------------------
  # Write output
  # -------------------------------------------------------------------------
  out_filename <- paste0(sensor_id, "_HPV.csv")
  out_path     <- file.path(hpv_dir, out_filename)
  write_csv(results_df, out_path)
  message(sprintf("  Written: %s  (%d pulses, %d skipped)",
                  out_filename, nrow(results_df), n_skipped))
  
  # -------------------------------------------------------------------------
  # Summary log entry
  # -------------------------------------------------------------------------
  summary_log[[sensor_id]] <- data.frame(
    SensorID                 = sensor_id,
    treeID                   = tree_id,
    k                        = k,
    x                        = x,
    d_wound                  = d_wound,
    WoundCorrectionApplied   = wound_correction_applied,
    TotalPulsesProcessed     = nrow(results_df),
    PulsesSkipped            = n_skipped,
    Flagged_LowVoltage       = sum(results_df$LowVoltage_flag, na.rm = TRUE),
    Flagged_ShortPulse       = sum(results_df$ShortPulse_flag, na.rm = TRUE),
    Flagged_ThermOutOfRange  = sum(results_df$ThermOutOfRange_flag, na.rm = TRUE),
    Flagged_HeaterDidNotFire = sum(results_df$HeaterDidNotFire_flag, na.rm = TRUE)
  )
}

# =============================================================================
# WRITE SUMMARY LOG
# =============================================================================
if(length(summary_log) == 0) {
  message("\nNo sensors were successfully processed. Check messages above.")
} else {
  summary_df <- bind_rows(summary_log)
  write_csv(summary_df, file.path(hpv_dir, "HPV_Summary.csv"))
  message("\nDone. Summary written to 04_HPV/HPV_Summary.csv")
}
