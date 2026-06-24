# =============================================================================
# 04_SapFlux.R  [OPTIONAL]
# Js5 Sap Flow Sensor V2.1 Processing Pipeline
# CUNY Advanced Science Research Center | Reinmann Lab | Next Generation Sensor Lab
#
# PURPOSE:
#   Scales wound-corrected heat pulse velocity (HPV, cm/hr) to volumetric
#   sap flux (cm³/hr) using sapwood area. Outputs one processed .csv per
#   sensor to 05_SapFlux/.
#
# THIS SCRIPT IS OPTIONAL.
#   It requires additional tree structural parameters (sapwood area or
#   allometric coefficients + DBH) that may not be available for all
#   deployments. Scripts 01-03 are complete without running this script.
#
# METHOD:
#   Volumetric sap flux (cm³/hr) = Vhrm_corr (cm/hr) * sapwood area (cm²)
#
#   Sapwood area is either:
#     (a) Measured directly from increment cores (SapwoodArea_cm2 in
#         TreeParameters — preferred), or
#     (b) Estimated from DBH using Bovard et al. (2005) allometry:
#             A_s (cm²) = B_o * DBH_cm - B_i
#         Species-specific B_o and B_i values are required (see
#         TreeParameters README for sources).
#
#   IMPORTANT NOTE ON DEPTH SELECTION:
#   This script outputs volumetric sap flux calculated from ALL THREE
#   thermistor depths (outer, middle, inner) separately. Which depth(s)
#   are appropriate for your analysis depends on the wood anatomy of your
#   study species:
#
#   Ring-porous species (e.g. oaks, ashes, elms):
#     Sap flow is concentrated in the outermost growth ring(s). The outer
#     thermistor (5 mm depth) is most appropriate. Middle and inner depths
#     may show little or no flow and should be interpreted with caution.
#
#   Diffuse-porous species (e.g. maples, birches, beeches):
#     Sap flow occurs throughout the sapwood. All three depths may show
#     meaningful flow. A weighted average across depths based on annular
#     ring area is more appropriate than using a single depth.
#
#   If you are unsure of your species' wood anatomy, consult the primary
#   literature before selecting a depth for analysis. See Burgess et al.
#   (2001) and Meinzer et al. (2001) for guidance.
#
# INPUT:
#   - HPV .csv files in 04_HPV/ (output of 02_ProcessHPV.R)
#   - TreeParameters.csv in root project folder
#
# OUTPUT:
#   - One sap flux .csv per sensor in 05_SapFlux/
#   - A summary log: 05_SapFlux/SapFlux_Summary.csv
#
# COLUMN GUIDE FOR OUTPUT FILES:
#   SensorID            : Device ID
#   treeID              : Tree identifier from TreeParameters
#   species             : Species code from TreeParameters
#   Pulse_DateTime      : Timestamp of pulse
#   PulseNum            : Sequential pulse number
#   Pulse_AvgVoltage    : Mean battery voltage for pulse
#   Vhrm_outer_corr     : Wound-corrected HPV, outer depth (cm/hr)
#   Vhrm_middle_corr    : Wound-corrected HPV, middle depth (cm/hr)
#   Vhrm_inner_corr     : Wound-corrected HPV, inner depth (cm/hr)
#   SapwoodArea_cm2     : Sapwood area used in calculation (cm²)
#   SapwoodArea_source  : "measured" if from TreeParameters directly,
#                         "allometry_Bovard2005" if estimated from DBH
#   F_outer_cm3hr       : Volumetric sap flux, outer depth (cm³/hr)
#   F_middle_cm3hr      : Volumetric sap flux, middle depth (cm³/hr)
#   F_inner_cm3hr       : Volumetric sap flux, inner depth (cm³/hr)
#   DBH_cm              : DBH used in allometry (if applicable)
#   LowVoltage_flag     : Carried from Script 02
#   ShortPulse_flag     : Carried from Script 02
#   ThermOutOfRange_flag     : Carried from Script 02
#   HeaterDidNotFire_flag    : Carried from Script 02
#
# REFERENCES:
#   Bovard, B.D., Curtis, P.S., Vogel, C.S., Su, H.-B., and Schmid, H.P.
#     (2005). Environmental controls on sap flow in a northern hardwood
#     forest. Tree Physiology 25(1): 31-38.
#   Burgess, S.S.O., Adams, M.A., Turner, N.C., Beverly, C.R., Ong, C.K.,
#     Khan, A.A.H., and Bleby, T.M. (2001). An improved heat pulse method
#     to measure low and reverse rates of sap flow in woody plants.
#     Tree Physiology 21(9): 589-598.
#   Meinzer, F.C., Clearwater, M.J., and Goldstein, G. (2001). Water
#     transport in trees: current perspectives, new insights and some
#     controversies. Environmental and Experimental Botany 45(3): 239-262.
# =============================================================================

library(tidyverse)
library(lubridate)

# =============================================================================
# PATHS — UPDATE THESE FOR YOUR SYSTEM
# =============================================================================
root_dir      <- "C:/Users/eviea/Graduate Center Dropbox/Evonne Aguirre/NSF_HF_Shared_Project_Folder/CLIFF_Data/Trees/Sap Flow/SF protocols/V2.1 Code Pipeline"   # <-- UPDATE THIS
hpv_dir       <- file.path(root_dir, "04_HPV")
sapflux_dir   <- file.path(root_dir, "05_SapFlux")
params_path   <- file.path(root_dir, "TreeParameters.csv")

dir.create(sapflux_dir, showWarnings = FALSE)

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

# Check required columns
required_cols <- c("sensorID", "treeID")
missing_cols  <- required_cols[!required_cols %in% names(params)]

if(length(missing_cols) > 0) {
  stop(
    "\nTreeParameters.csv is missing required column(s): ",
    paste(missing_cols, collapse = ", "),
    "\nPlease check your TreeParameters.csv and try again."
  )
}

# =============================================================================
# CHECK FOR HPV FILES
# =============================================================================
hpv_files <- list.files(hpv_dir, pattern = "_HPV\\.csv$",
                         full.names = TRUE, ignore.case = TRUE)

if(length(hpv_files) == 0) {
  stop(
    "\nNo HPV files found in:\n  ", hpv_dir,
    "\nExpected files ending in '_HPV.csv'.",
    "\nMake sure you have run 02_ProcessHPV.R first."
  )
}

# =============================================================================
# MAIN LOOP — process each HPV file
# =============================================================================
summary_log <- list()

for(f in hpv_files) {
  
  sensor_id <- sub("_HPV\\.csv$", "", basename(f), ignore.case = TRUE)
  message(paste("Processing:", sensor_id))
  
  # -------------------------------------------------------------------------
  # Match to TreeParameters
  # -------------------------------------------------------------------------
  tree_params <- params %>% filter(sensorID == sensor_id)
  
  if(nrow(tree_params) == 0) {
    message(sprintf(
      "  Skipping %s — no matching row found in TreeParameters.csv.\n  Check that sensorID '%s' is spelled correctly.",
      sensor_id, sensor_id
    ))
    next
  }
  
  if(nrow(tree_params) > 1) {
    message(sprintf(
      "  Warning: multiple rows found for sensorID '%s'. Using first row.",
      sensor_id
    ))
    tree_params <- tree_params[1, ]
  }
  
  # Extract parameters
  tree_id <- tree_params$treeID
  species <- ifelse("species" %in% names(tree_params),
                    tree_params$species, NA)
  dbh_cm  <- ifelse("DBH_cm" %in% names(tree_params),
                    tree_params$DBH_cm, NA)
  
  # -------------------------------------------------------------------------
  # Determine sapwood area
  # Priority: SapwoodArea_cm2 (measured) > Bovard allometry
  # -------------------------------------------------------------------------
  sapwood_area   <- NA
  sapwood_source <- NA
  
  # Check for directly measured sapwood area
  if("SapwoodArea_cm2" %in% names(tree_params)) {
    sw_direct <- tree_params$SapwoodArea_cm2
    if(!is.na(sw_direct) && sw_direct > 0) {
      sapwood_area   <- sw_direct
      sapwood_source <- "measured"
    }
  }
  
  # If not measured, try Bovard allometry
  if(is.na(sapwood_area)) {
    
    has_bovard <- all(c("B_o", "B_i", "DBH_cm") %in% names(tree_params))
    
    if(has_bovard) {
      b_o   <- tree_params$B_o
      b_i   <- tree_params$B_i
      dbh   <- tree_params$DBH_cm
      
      if(!is.na(b_o) && !is.na(b_i) && !is.na(dbh)) {
        
        sw_allom <- b_o * dbh - b_i
        
        if(sw_allom <= 0) {
          message(sprintf(
            "  Skipping %s — Bovard allometry returned non-positive sapwood area (%.2f cm²).\n  DBH may be too small for this species equation. Check DBH_cm, B_o, and B_i in TreeParameters.",
            sensor_id, sw_allom
          ))
          next
        }
        
        sapwood_area   <- sw_allom
        sapwood_source <- "allometry_Bovard2005"
        
      } else {
        message(sprintf(
          "  Skipping %s — B_o, B_i, or DBH_cm is blank in TreeParameters.\n  Either fill in these columns for allometric estimation, or provide SapwoodArea_cm2 directly.",
          sensor_id
        ))
        next
      }
      
    } else {
      message(sprintf(
        "  Skipping %s — no sapwood area available.\n  Add either SapwoodArea_cm2 (measured) or B_o + B_i + DBH_cm (allometry) to TreeParameters.",
        sensor_id
      ))
      next
    }
  }
  
  message(sprintf("  Sapwood area: %.2f cm² (%s)", sapwood_area, sapwood_source))
  
  # -------------------------------------------------------------------------
  # Read HPV data
  # -------------------------------------------------------------------------
  alldata <- tryCatch({
    read_csv(f, show_col_types = FALSE)
  }, error = function(e) {
    message(sprintf("  Skipping %s — could not read HPV file: %s",
                    sensor_id, e$message))
    return(NULL)
  })
  
  if(is.null(alldata)) next
  
  alldata <- alldata %>%
    mutate(Pulse_DateTime = as.POSIXct(Pulse_DateTime,
                                        tryFormats = c("%Y-%m-%dT%H:%M:%SZ",
                                                       "%Y-%m-%d %H:%M:%S"),
                                        tz = "UTC")) %>%
    arrange(Pulse_DateTime)
  
  # -------------------------------------------------------------------------
  # Calculate volumetric sap flux for all three depths
  # F (cm³/hr) = Vhrm_corr (cm/hr) * sapwood_area (cm²)
  # -------------------------------------------------------------------------
  alldata <- alldata %>%
    mutate(
      SapwoodArea_cm2    = sapwood_area,
      SapwoodArea_source = sapwood_source,
      F_outer_cm3hr      = round(Vhrm_outer_corr  * sapwood_area, 4),
      F_middle_cm3hr     = round(Vhrm_middle_corr * sapwood_area, 4),
      F_inner_cm3hr      = round(Vhrm_inner_corr  * sapwood_area, 4),
      species            = species,
      DBH_cm             = dbh_cm
    )
  
  # -------------------------------------------------------------------------
  # Select and order output columns
  # -------------------------------------------------------------------------
  output <- alldata %>%
    select(SensorID, treeID, species, Pulse_DateTime, PulseNum,
           Pulse_AvgVoltage,
           Vhrm_outer_corr, Vhrm_middle_corr, Vhrm_inner_corr,
           SapwoodArea_cm2, SapwoodArea_source,
           F_outer_cm3hr, F_middle_cm3hr, F_inner_cm3hr,
           DBH_cm,
           LowVoltage_flag, ShortPulse_flag,
           ThermOutOfRange_flag, HeaterDidNotFire_flag)
  
  # -------------------------------------------------------------------------
  # Write output
  # -------------------------------------------------------------------------
  out_filename <- paste0(sensor_id, "_SapFlux.csv")
  out_path     <- file.path(sapflux_dir, out_filename)
  write_csv(output, out_path)
  message(sprintf("  Written: %s  (%d pulses)", out_filename, nrow(output)))
  
  # -------------------------------------------------------------------------
  # Summary log entry
  # -------------------------------------------------------------------------
  summary_log[[sensor_id]] <- data.frame(
    SensorID           = sensor_id,
    treeID             = tree_id,
    species            = ifelse(is.na(species), "unknown", as.character(species)),
    DBH_cm             = ifelse(is.na(dbh_cm), NA, dbh_cm),
    SapwoodArea_cm2    = sapwood_area,
    SapwoodArea_source = sapwood_source,
    TotalPulses        = nrow(output)
  )
}

# =============================================================================
# WRITE SUMMARY LOG
# =============================================================================
if(length(summary_log) == 0) {
  message("\nNo sensors were successfully processed. Check messages above.")
} else {
  summary_df <- bind_rows(summary_log)
  write_csv(summary_df, file.path(sapflux_dir, "SapFlux_Summary.csv"))
  message("\nDone. Summary written to 05_SapFlux/SapFlux_Summary.csv")
}
