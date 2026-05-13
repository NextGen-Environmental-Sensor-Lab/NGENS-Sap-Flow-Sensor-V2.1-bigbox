# =============================================================================
# 03_Plots.R
# Js5 Sap Flow Sensor V2.1 Processing Pipeline
# CUNY Advanced Science Research Center | Reinmann Lab | Next Generation Sensor Lab
#
# PURPOSE:
#   Generates diagnostic plots for quality control and data visualization.
#   Three sections:
#     Section 1: Battery voltage over time (one plot per sensor)
#     Section 2: HPV time series (one plot per sensor, all three depths)
#     Section 3: Per-pulse temperature plots (one PDF per sensor, one page
#                per pulse) — COMMENTED OUT BY DEFAULT, see note below.
#
# INPUT:
#   - Cleaned .csv files in 03_CleanedData/ (Sections 1 and 3)
#   - HPV .csv files in 04_HPV/ (Section 2)
#
# OUTPUT:
#   - 05_Plots/BatteryVoltage_[SensorID].pdf  (Section 1)
#   - 05_Plots/HPV_TimeSeries_[SensorID].pdf  (Section 2)
#   - 05_Plots/PulsePlots_[SensorID].pdf      (Section 3, if enabled)
#
# NOTE ON SECTION 3:
#   Per-pulse plots generate one page per pulse. For a full growing season
#   (~3,000 pulses per sensor) this can take 10-20 minutes per sensor and
#   produce very large PDF files. It is commented out by default.
#   To enable, remove the block comment markers (if(){} wrapper) around
#   Section 3. Run on a single sensor first to check output before
#   running on all sensors.
# =============================================================================

library(tidyverse)
library(lubridate)

# =============================================================================
# PATHS — UPDATE THESE FOR YOUR SYSTEM
# =============================================================================
root_dir     <- "C:/Users/eviea/Graduate Center Dropbox/Evonne Aguirre/NSF_HF_Shared_Project_Folder/CLIFF_Data/Trees/Sap Flow/SF protocols/V2.1 Code Pipeline"   # <-- UPDATE THIS
cleaned_dir  <- file.path(root_dir, "03_CleanedData")
hpv_dir      <- file.path(root_dir, "04_HPV")
plot_dir     <- file.path(root_dir, "05_Plots")

dir.create(plot_dir, showWarnings = FALSE)

# =============================================================================
# PARAMETERS
# =============================================================================
# Voltage reference line on battery plots (V)
# Set to your low-voltage threshold from Script 01 for visual reference
VOLTAGE_THRESHOLD <- 10.5

# Post-heat window boundaries (seconds) — for vertical lines on pulse plots
POSTHEAT_WIN_START <- 60
POSTHEAT_WIN_END   <- 100

# =============================================================================
# SECTION 1: BATTERY VOLTAGE OVER TIME
# Reads from 03_CleanedData/
# One PDF per sensor showing mean pulse voltage over the season
# Useful for tracking battery health and identifying when packs need swapping
# =============================================================================
message("--- Section 1: Battery voltage plots ---")

cleaned_files <- list.files(cleaned_dir, pattern = "_cleaned\\.csv$",
                             full.names = TRUE, ignore.case = TRUE)

if(length(cleaned_files) == 0) {
  message("No cleaned files found in 03_CleanedData/. Skipping Section 1.")
} else {
  
  for(f in cleaned_files) {
    
    sensor_id <- sub("_cleaned\\.csv$", "", basename(f), ignore.case = TRUE)
    message(paste("  Battery plot:", sensor_id))
    
    alldata <- tryCatch(
      read_csv(f, show_col_types = FALSE),
      error = function(e) {
        message(paste("  Could not read", basename(f), "—", e$message))
        return(NULL)
      }
    )
    if(is.null(alldata)) next
    
    alldata <- alldata %>%
      mutate(DateTime = as.POSIXct(DateTime, format = "%Y-%m-%d %H:%M:%S",
                                    tz = "UTC"))
    
    # Summarise to one voltage value per pulse (mean)
    volt_summary <- alldata %>%
      group_by(PulseNum) %>%
      summarise(
        DateTime    = min(DateTime, na.rm = TRUE),
        MeanVoltage = mean(Voltage, na.rm = TRUE),
        .groups = "drop"
      ) %>%
      arrange(DateTime)
    
    out_path <- file.path(plot_dir,
                           paste0("BatteryVoltage_", sensor_id, ".pdf"))
    pdf(out_path, width = 10, height = 5)
    
    plot(volt_summary$MeanVoltage ~ volt_summary$DateTime,
         type = "l", lwd = 1.2, col = "steelblue",
         xlab = "Date", ylab = "Mean Pulse Voltage (V)",
         main = paste("Battery Voltage Over Time —", sensor_id),
         ylim = c(min(c(volt_summary$MeanVoltage, VOLTAGE_THRESHOLD),
                      na.rm = TRUE) - 0.5,
                  max(volt_summary$MeanVoltage, na.rm = TRUE) + 0.5))
    
    # Reference line at low-voltage threshold
    abline(h = VOLTAGE_THRESHOLD, col = "firebrick", lty = 2, lwd = 1.5)
    legend("topright",
           legend = c("Mean pulse voltage",
                      paste0("Low voltage threshold (", VOLTAGE_THRESHOLD, "V)")),
           col    = c("steelblue", "firebrick"),
           lty    = c(1, 2), lwd = c(1.2, 1.5),
           bty    = "n", cex = 0.8)
    
    dev.off()
    message(paste("  Written:", basename(out_path)))
  }
}

# =============================================================================
# SECTION 2: HPV TIME SERIES
# Reads from 04_HPV/
# One PDF per sensor showing Vhrm_outer_corr, Vhrm_middle_corr, Vhrm_inner_corr
# over time on one panel
# =============================================================================
message("--- Section 2: HPV time series plots ---")

hpv_files <- list.files(hpv_dir, pattern = "_HPV\\.csv$",
                          full.names = TRUE, ignore.case = TRUE)

if(length(hpv_files) == 0) {
  message("No HPV files found in 04_HPV/. Skipping Section 2.")
} else {
  
  for(f in hpv_files) {
    
    sensor_id <- sub("_HPV\\.csv$", "", basename(f), ignore.case = TRUE)
    message(paste("  HPV plot:", sensor_id))
    
    alldata <- tryCatch(
      read_csv(f, show_col_types = FALSE),
      error = function(e) {
        message(paste("  Could not read", basename(f), "—", e$message))
        return(NULL)
      }
    )
    if(is.null(alldata)) next
    
    alldata <- alldata %>%
      mutate(Pulse_DateTime = as.POSIXct(Pulse_DateTime, tz = "UTC")) %>%
      arrange(Pulse_DateTime)
    
    # Replace Inf with NA (can occur if ΔT ratio is undefined)
    alldata <- alldata %>%
      mutate(
        Vhrm_outer_corr  = ifelse(is.infinite(Vhrm_outer_corr),  NA, Vhrm_outer_corr),
        Vhrm_middle_corr = ifelse(is.infinite(Vhrm_middle_corr), NA, Vhrm_middle_corr),
        Vhrm_inner_corr  = ifelse(is.infinite(Vhrm_inner_corr),  NA, Vhrm_inner_corr)
      )
    
    # Y axis range across all three depths
    all_vals <- c(alldata$Vhrm_outer_corr,
                  alldata$Vhrm_middle_corr,
                  alldata$Vhrm_inner_corr)
    plot_ymin <- min(all_vals, na.rm = TRUE)
    plot_ymax <- max(all_vals, na.rm = TRUE)
    
    # Pad y axis slightly
    y_pad     <- (plot_ymax - plot_ymin) * 0.05
    plot_ymin <- plot_ymin - y_pad
    plot_ymax <- plot_ymax + y_pad
    
    out_path <- file.path(plot_dir,
                           paste0("HPV_TimeSeries_", sensor_id, ".pdf"))
    pdf(out_path, width = 12, height = 5)
    
    plot(alldata$Vhrm_outer_corr ~ alldata$Pulse_DateTime,
         type = "n",
         ylim = c(plot_ymin, plot_ymax),
         xlab = "Date",
         ylab = "Wound-corrected HPV (cm/hr)",
         main = paste("HPV Time Series —", sensor_id))
    
    lines(alldata$Vhrm_outer_corr  ~ alldata$Pulse_DateTime,
          col = "firebrick", lwd = 0.8)
    lines(alldata$Vhrm_middle_corr ~ alldata$Pulse_DateTime,
          col = "forestgreen", lwd = 0.8)
    lines(alldata$Vhrm_inner_corr  ~ alldata$Pulse_DateTime,
          col = "steelblue", lwd = 0.8)
    
    abline(h = 0, col = "gray60", lty = 2, lwd = 0.8)
    
    legend("topright",
           legend = c("Outer (5 mm)", "Middle (17.5 mm)", "Inner (30 mm)"),
           col    = c("firebrick", "forestgreen", "steelblue"),
           lty    = 1, lwd = 1.5,
           bty    = "n", cex = 0.8)
    
    dev.off()
    message(paste("  Written:", basename(out_path)))
  }
}

# =============================================================================
# SECTION 3: PER-PULSE TEMPERATURE PLOTS
#
#     SLOW — commented out by default.
#     For a full season (~3,000 pulses), this takes 10-20 min per sensor
#     and produces large PDF files.
#     To enable: remove the if(FALSE){ ... } wrapper below.
#     Recommended: test on one sensor first by setting
#     cleaned_files_pulse to a single file path.
# =============================================================================

if(FALSE){
  
  message("--- Section 3: Per-pulse temperature plots ---")
  
  cleaned_files_pulse <- list.files(cleaned_dir, pattern = "_cleaned\\.csv$",
                                     full.names = TRUE, ignore.case = TRUE)
  
  if(length(cleaned_files_pulse) == 0) {
    message("No cleaned files found in 03_CleanedData/. Skipping Section 3.")
  } else {
    
    for(f in cleaned_files_pulse) {
      
      sensor_id <- sub("_cleaned\\.csv$", "", basename(f), ignore.case = TRUE)
      message(paste("  Pulse plots:", sensor_id))
      
      alldata <- tryCatch(
        read_csv(f, show_col_types = FALSE),
        error = function(e) {
          message(paste("  Could not read", basename(f), "—", e$message))
          return(NULL)
        }
      )
      if(is.null(alldata)) next
      
      alldata <- alldata %>%
        mutate(DateTime = as.POSIXct(DateTime, format = "%Y-%m-%d %H:%M:%S",
                                      tz = "UTC")) %>%
        arrange(DateTime)
      
      out_path <- file.path(plot_dir,
                             paste0("PulsePlots_", sensor_id, ".pdf"))
      pdf(out_path, width = 7, height = 6)
      
      pulse_nums <- sort(unique(alldata$PulseNum))
      
      for(pnum in pulse_nums) {
        
        focal_pulse <- alldata %>% filter(PulseNum == pnum)
        focal_pulse <- focal_pulse %>%
          mutate(ElapsedTime = seq(0, n() - 1, 1))
        
        pulse_datetime <- min(focal_pulse$DateTime, na.rm = TRUE)
        avg_voltage    <- mean(focal_pulse$Voltage, na.rm = TRUE)
        
        # Separate phases
        preheat  <- focal_pulse %>% filter(trimws(Flag) == "pre-heat")
        postheat <- focal_pulse %>% filter(trimws(Flag) == "post-heat")
        postheat <- postheat %>% mutate(ElapsedTime2 = seq(0, n() - 1, 1))
        
        # Locate 60-100s window in elapsed time of full pulse
        postheat_win <- postheat %>%
          filter(ElapsedTime2 >= POSTHEAT_WIN_START &
                   ElapsedTime2 <= POSTHEAT_WIN_END)
        
        # Check for all-NA thermistor data
        therm_vals <- c(focal_pulse$NO, focal_pulse$NM, focal_pulse$NI,
                        focal_pulse$FO, focal_pulse$FM, focal_pulse$FI)
        
        plot_title <- paste0(format(pulse_datetime, "%Y-%m-%d %H:%M"),
                             "  |  Pulse ", pnum,
                             "  |  Avg voltage: ", round(avg_voltage, 2), "V")
        
        if(sum(!is.na(therm_vals)) == 0) {
          
          # Empty pulse — placeholder plot
          plot(1, type = "n", xlab = "Elapsed Time (s)", ylab = "Temp (°C)",
               main = plot_title)
          text(1, 1, "All thermistor values are NA for this pulse", cex = 0.9)
          
        } else {
          
          plot_ymin <- min(therm_vals, na.rm = TRUE)
          plot_ymax <- max(therm_vals, na.rm = TRUE)
          
          # Base plot — upstream outer (NO)
          plot(focal_pulse$NO ~ focal_pulse$ElapsedTime,
               type  = "p", pch = 20, cex = 0.4,
               col   = "firebrick",
               ylim  = c(plot_ymin, plot_ymax),
               xlab  = "Elapsed Time (s)",
               ylab  = "Temp (°C)",
               main  = plot_title)
          
          # Add remaining thermistors
          points(focal_pulse$NM ~ focal_pulse$ElapsedTime,
                 pch = 20, cex = 0.4, col = "forestgreen")
          points(focal_pulse$NI ~ focal_pulse$ElapsedTime,
                 pch = 20, cex = 0.4, col = "steelblue")
          points(focal_pulse$FO ~ focal_pulse$ElapsedTime,
                 pch = 20, cex = 0.4, col = "firebrick4")
          points(focal_pulse$FM ~ focal_pulse$ElapsedTime,
                 pch = 20, cex = 0.4, col = "darkgreen")
          points(focal_pulse$FI ~ focal_pulse$ElapsedTime,
                 pch = 20, cex = 0.4, col = "navy")
          
          # Vertical lines: pre-heat end and post-heat window (60-100s)
          preheat_end_time <- max(preheat$ElapsedTime, na.rm = TRUE)
          
          if(nrow(postheat_win) > 0) {
            win_start_elapsed <- min(postheat_win$ElapsedTime, na.rm = TRUE)
            win_end_elapsed   <- max(postheat_win$ElapsedTime, na.rm = TRUE)
            abline(v = win_start_elapsed, col = "gray40", lty = 2, lwd = 1.2)
            abline(v = win_end_elapsed,   col = "gray40", lty = 2, lwd = 1.2)
          }
          
          legend("topleft",
                 legend = c("NO (Up-Outer)", "NM (Up-Mid)", "NI (Up-Inner)",
                             "FO (Dn-Outer)", "FM (Dn-Mid)", "FI (Dn-Inner)"),
                 col    = c("firebrick", "forestgreen", "steelblue",
                             "firebrick4", "darkgreen", "navy"),
                 pch = 20, pt.cex = 1.0,
                 bty = "n", horiz = FALSE, cex = 0.55)
        }
      }
      
      dev.off()
      message(paste("  Written:", basename(out_path)))
    }
  }
  
} # end Section 3 (remove if(FALSE){ } to enable)
