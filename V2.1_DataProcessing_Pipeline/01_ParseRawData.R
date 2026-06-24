# =============================================================================
# 01_ParseRawData.R
# Js5 Sap Flow Sensor V2.1 Processing Pipeline
# CUNY Advanced Science Research Center | Reinmann Lab | Next Generation Sensor Lab
#
# PURPOSE:
#   Reads raw .csv files from the Js5 V2.1 datalogger, parses the mixed
#   metadata/data format, assigns pulse numbers chronologically, applies
#   quality control flags, and writes one clean .csv per sensor to
#   03_CleanedData/.
#
# INPUT:
#   - Raw .csv files in 02_RawData_Current/ (one file per sensor)
#   - TreeParameters.csv in root project folder
#
# OUTPUT:
#   - One cleaned .csv per sensor in 03_CleanedData/
#   - A summary log: 03_CleanedData/ParseSummary.csv
#
# COLUMN GUIDE FOR OUTPUT FILES:
#   SensorID             : Device ID from file header (e.g. SFS10)
#   DateTime             : Timestamp for each 1-second measurement row
#   PulseNum             : Sequential pulse number, assigned chronologically
#   NO                   : Near needle, Outer thermistor (upstream, 5 mm depth)
#   NM                   : Near needle, Middle thermistor (upstream, 17.5 mm depth)
#   NI                   : Near needle, Inner thermistor (upstream, 30 mm depth)
#   FO                   : Far needle, Outer thermistor (downstream, 5 mm depth)
#   FM                   : Far needle, Middle thermistor (downstream, 17.5 mm depth)
#   FI                   : Far needle, Inner thermistor (downstream, 30 mm depth)
#   Voltage              : Battery voltage (V) recorded each second
#   Heat_mA              : Heater current (mA); ~0.296 during heat phase, 0 otherwise
#   Flag                 : Measurement phase label (pre-heat, heat, post-heat)
#   ExpectedRows         : Total rows expected based on header timing parameters
#   ActualRows           : Rows actually recorded for this pulse
#   LowVoltage_flag      : 1 if mean pulse voltage below VOLTAGE_THRESHOLD, else 0
#   ShortPulse_flag      : 1 if ActualRows < 90% of ExpectedRows, else 0
#   ThermOutOfRange_flag : 1 if any thermistor reading outside TEMP_MIN-TEMP_MAX, else 0
#   HeaterDidNotFire_flag: 1 if no heat_mA > 0 detected during heat phase, else 0
#
# THERMISTOR PAIRING FOR HPV CALCULATION (Script 02):
#   Outer depth  : upstream = NO, downstream = FO
#   Middle depth : upstream = NM, downstream = FM
#   Inner depth  : upstream = NI, downstream = FI
#
# WORKFLOW NOTE:
#   After each weekly data download:
#     1. Archive downloaded files in 01_RawData_Archive/YYYY-MM-DD/
#     2. Copy downloaded files into 02_RawData_Current/ (replacing old files)
#     3. Re-run this script
#   Flagged pulses are retained in output — they are NOT removed.
#   Review ParseSummary.csv after each run to check for unexpected issues.
# =============================================================================

library(tidyverse)
library(lubridate)

# =============================================================================
# PATHS — UPDATE THESE FOR YOUR SYSTEM
# =============================================================================
root_dir     <- "C:/Users/eviea/Graduate Center Dropbox/Evonne Aguirre/NSF_HF_Shared_Project_Folder/CLIFF_Data/Trees/Sap Flow/SF protocols/V2.1 Code Pipeline"   # <-- UPDATE THIS
raw_data_dir <- file.path(root_dir, "02_RawData_Current")
cleaned_dir  <- file.path(root_dir, "03_CleanedData")
params_path  <- file.path(root_dir, "TreeParameters.csv")

dir.create(cleaned_dir, showWarnings = FALSE)

# =============================================================================
# PARAMETERS — REVIEW BEFORE RUNNING
# =============================================================================

# Voltage threshold for low battery flag.
# Pulses with mean voltage below this value will be flagged.
# Default: 10.5V. Verify against your battery pack's observed discharge curve.
VOLTAGE_THRESHOLD <- 10.5

# Thermistor plausible range (degrees C).
# Readings outside this range will trigger ThermOutOfRange_flag.
TEMP_MIN <- 0
TEMP_MAX <- 50

# Short pulse tolerance.
# Pulses with fewer than this proportion of expected rows will be flagged.
# Default: 0.90 (flags pulses missing more than 10% of expected data rows).
SHORT_PULSE_TOLERANCE <- 0.90

# =============================================================================
# LOAD TREE PARAMETERS
# =============================================================================
params <- read_csv(params_path, show_col_types = FALSE)

# =============================================================================
# HELPER FUNCTION: Parse one raw V2.1 csv file
# =============================================================================
parse_sfs_file <- function(filepath) {
  
  # Read all lines as raw text
  raw_lines <- readLines(filepath, warn = FALSE)
  
  # Remove blank lines
  raw_lines <- raw_lines[nchar(trimws(raw_lines)) > 0]
  
  # ---------------------------------------------------------------------------
  # Extract SensorID from first M- header block
  # Header line format: "M- Starting Event on Device SFS10"
  # ---------------------------------------------------------------------------
  id_line_idx <- grep("Starting Event on Device", raw_lines)[1]
  if(is.na(id_line_idx)) {
    warning(paste("Could not find Device ID in", filepath))
    return(NULL)
  }
  sensor_id <- trimws(sub(".*Device\\s+", "", raw_lines[id_line_idx]))
  
  # ---------------------------------------------------------------------------
  # Split file into individual pulse blocks
  # Each block starts with a line of asterisks: "M- *****..."
  # ---------------------------------------------------------------------------
  block_starts <- grep("\\*{5,}", raw_lines)
  n_blocks <- length(block_starts)
  
  if(n_blocks == 0) {
    warning(paste("No pulse blocks found in", filepath))
    return(NULL)
  }
  
  # ---------------------------------------------------------------------------
  # Loop over pulse blocks and parse each one
  # ---------------------------------------------------------------------------
  all_pulses <- list()
  
  for(b in seq_along(block_starts)) {
    
    # Define line range for this block
    start_line  <- block_starts[b]
    end_line    <- ifelse(b < n_blocks, block_starts[b + 1] - 1, length(raw_lines))
    block_lines <- raw_lines[start_line:end_line]
    
    # -------------------------------------------------------------------------
    # Extract timing parameters from M- header
    # Line format: "M- YYYY/MM/DD HH:MM:SS T_MINS=30 PREH_SECS=20 H_SECS=2 POSTH_SECS=120"
    # -------------------------------------------------------------------------
    timing_line_idx <- grep("T_MINS", block_lines)[1]
    
    if(is.na(timing_line_idx)) next  # skip malformed blocks
    
    timing_line <- block_lines[timing_line_idx]
    preh_secs   <- as.numeric(sub(".*PREH_SECS=(\\d+).*",  "\\1", timing_line))
    h_secs      <- as.numeric(sub(".*H_SECS=(\\d+).*",     "\\1", timing_line))
    posth_secs  <- as.numeric(sub(".*POSTH_SECS=(\\d+).*", "\\1", timing_line))
    expected_rows <- preh_secs + h_secs + posth_secs
    
    # -------------------------------------------------------------------------
    # Extract data rows
    # Data rows start with a date (YYYY/MM/DD); M- lines are metadata
    # -------------------------------------------------------------------------
    data_lines <- block_lines[!grepl("^M-", block_lines) &
                                grepl("^\\d{4}/\\d{2}/\\d{2}", block_lines)]
    
    if(length(data_lines) == 0) next
    
    # Parse into data frame
    pulse_df <- tryCatch({
      read.csv(
        text      = paste(data_lines, collapse = "\n"),
        header    = FALSE,
        strip.white = TRUE,
        col.names = c("DateTime", "NO", "NM", "NI",
                      "FO", "FM", "FI", "Voltage", "Heat_mA", "Flag")
      )
    }, error = function(e) {
      warning(paste("Could not parse block", b, "in", filepath, ":", e$message))
      return(NULL)
    })
    
    if(is.null(pulse_df)) next
    
    # Parse datetime
    pulse_df$DateTime <- as.POSIXct(pulse_df$DateTime,
                                     format = "%Y/%m/%d %H:%M:%S",
                                     tz = "UTC")
    
    # Add metadata columns
    pulse_df$SensorID     <- sensor_id
    pulse_df$PulseNum     <- b        # renumbered chronologically after combining
    pulse_df$ExpectedRows <- expected_rows
    pulse_df$ActualRows   <- nrow(pulse_df)
    
    all_pulses[[b]] <- pulse_df
  }
  
  # ---------------------------------------------------------------------------
  # Combine all pulses into one data frame
  # ---------------------------------------------------------------------------
  if(length(all_pulses) == 0) {
    warning(paste("No valid pulses parsed from", filepath))
    return(NULL)
  }
  
  combined <- bind_rows(all_pulses)
  
  # Renumber PulseNum chronologically by first timestamp of each pulse
  pulse_order <- combined %>%
    group_by(PulseNum) %>%
    summarise(first_dt = min(DateTime, na.rm = TRUE), .groups = "drop") %>%
    arrange(first_dt) %>%
    mutate(PulseNum_chrono = row_number())
  
  combined <- combined %>%
    left_join(pulse_order %>% select(PulseNum, PulseNum_chrono),
              by = "PulseNum") %>%
    mutate(PulseNum = PulseNum_chrono) %>%
    select(-PulseNum_chrono) %>%
    arrange(DateTime)
  
  return(combined)
}

# =============================================================================
# MAIN LOOP — process all sensor files in 02_RawData_Current/
# =============================================================================
raw_files <- list.files(raw_data_dir, pattern = "\\.csv$",
                         full.names = TRUE, ignore.case = TRUE)

if(length(raw_files) == 0) {
  stop("No .csv files found in 02_RawData_Current/. Check your path.")
}

summary_log <- list()

for(f in raw_files) {
  
  message(paste("Processing:", basename(f)))
  
  parsed <- parse_sfs_file(f)
  
  if(is.null(parsed)) {
    message(paste("  Skipping — no valid data parsed from", basename(f)))
    next
  }
  
  sensor_id <- unique(parsed$SensorID)
  
  # ---------------------------------------------------------------------------
  # QUALITY CONTROL FLAGS
  # Computed per pulse. Flagged pulses are retained — NOT removed.
  # ---------------------------------------------------------------------------
  therm_cols <- c("NO", "NM", "NI", "FO", "FM", "FI")
  
  qc_flags <- parsed %>%
    group_by(PulseNum) %>%
    summarise(
      mean_voltage   = mean(Voltage, na.rm = TRUE),
      expected_rows  = first(ExpectedRows),
      actual_rows    = first(ActualRows),
      therm_min      = min(c_across(all_of(therm_cols)), na.rm = TRUE),
      therm_max      = max(c_across(all_of(therm_cols)), na.rm = TRUE),
      # HeaterDidNotFire: TRUE if no row in the heat phase has Heat_mA > 0
      heater_fired   = any(Flag == "heat" & Heat_mA > 0, na.rm = TRUE),
      .groups = "drop"
    ) %>%
    mutate(
      LowVoltage_flag       = if_else(mean_voltage < VOLTAGE_THRESHOLD, 1L, 0L),
      ShortPulse_flag       = if_else(actual_rows < SHORT_PULSE_TOLERANCE * expected_rows, 1L, 0L),
      ThermOutOfRange_flag  = if_else(therm_min < TEMP_MIN | therm_max > TEMP_MAX, 1L, 0L),
      HeaterDidNotFire_flag = if_else(!heater_fired, 1L, 0L)
    ) %>%
    select(PulseNum, LowVoltage_flag, ShortPulse_flag,
           ThermOutOfRange_flag, HeaterDidNotFire_flag)
  
  parsed <- parsed %>%
    left_join(qc_flags, by = "PulseNum")
  
  # ---------------------------------------------------------------------------
  # SELECT AND ORDER OUTPUT COLUMNS
  # ---------------------------------------------------------------------------
  output <- parsed %>%
    select(SensorID, DateTime, PulseNum,
           NO, NM, NI, FO, FM, FI,
           Voltage, Heat_mA, Flag,
           ExpectedRows, ActualRows,
           LowVoltage_flag, ShortPulse_flag,
           ThermOutOfRange_flag, HeaterDidNotFire_flag)
  
  # ---------------------------------------------------------------------------
  # WRITE CLEANED FILE
  # ---------------------------------------------------------------------------
  out_filename <- paste0(sensor_id, "_cleaned.csv")
  out_path     <- file.path(cleaned_dir, out_filename)
  write_csv(output, out_path)
  message(paste("  Written:", out_filename))
  
  # ---------------------------------------------------------------------------
  # SUMMARY LOG ENTRY
  # ---------------------------------------------------------------------------
  n_pulses      <- length(unique(output$PulseNum))
  n_lowvolt     <- sum(qc_flags$LowVoltage_flag)
  n_short       <- sum(qc_flags$ShortPulse_flag)
  n_therm       <- sum(qc_flags$ThermOutOfRange_flag)
  n_nofire      <- sum(qc_flags$HeaterDidNotFire_flag)
  n_any_flagged <- sum(rowSums(qc_flags[, -1]) > 0)
  
  summary_log[[sensor_id]] <- data.frame(
    SensorID                  = sensor_id,
    SourceFile                = basename(f),
    TotalPulses               = n_pulses,
    Flagged_LowVoltage        = n_lowvolt,
    Flagged_ShortPulse        = n_short,
    Flagged_ThermOutOfRange   = n_therm,
    Flagged_HeaterDidNotFire  = n_nofire,
    TotalPulsesWithAnyFlag    = n_any_flagged
  )
  
  message(sprintf("  %d pulses | %d low voltage | %d short | %d therm out of range | %d heater did not fire",
                  n_pulses, n_lowvolt, n_short, n_therm, n_nofire))
}

# =============================================================================
# WRITE SUMMARY LOG
# =============================================================================
summary_df <- bind_rows(summary_log)
write_csv(summary_df, file.path(cleaned_dir, "ParseSummary.csv"))
message("\nDone. Summary written to 03_CleanedData/ParseSummary.csv")
