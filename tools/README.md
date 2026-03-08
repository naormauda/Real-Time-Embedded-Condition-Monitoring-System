# Training Data Collection & Model Training Tools

This directory contains tools for collecting real-world training data from the Smart Safe device and training anomaly detection models offline.

## Overview

The complete ML training pipeline:

```
1. Collect Data         → collect_training_data.py
   (NORMAL / VIBRATION / TAMPERING scenarios)
                ↓
2. Review & Label       → training_data/*.csv
                ↓
3. Train Model          → train_anomaly_model.py
   (Isolation Forest / One-Class SVM)
                ↓
4. Evaluate Performance → Metrics & Confusion Matrix
                ↓
5. Export to Embedded C → export_iforest_to_c.py
                ↓
6. Deploy to Device     → Build firmware with generated_iforest_model.c
```

---

## 1. Collecting Training Data

### Prerequisites

```bash
pip3 install pyserial
```

### Usage

```bash
python3 collect_training_data.py --port COM3 --baudrate 115200
```

**On Windows:** Replace `COM3` with your actual COM port (find via Device Manager)
**On Linux/Mac:** Use `/dev/ttyUSB0` or similar

### How It Works

1. **Connects to NUCLEO board** via serial (expects baud rate 115200)
2. **Parses feature extraction output** from processor (looks for `[FE]` lines)
3. **Labels scenarios** interactively:
   - **NORMAL**: Device at rest (30 seconds)
   - **VIBRATION**: Gentle movement/vibration (30 seconds)
   - **TAMPERING**: Rapid shaking/tamper attempts (30 seconds)
4. **Exports to CSV**: Creates timestamped feature files with labels

### Expected Output

```
=== Smart Safe Training Data Collector ===

Select a scenario to collect data for:
  [1] NORMAL      - Device at rest (no motion)
  [2] VIBRATION   - Gentle vibration/movement
  [3] TAMPERING   - Rapid shaking/attempts to open
  [4] View collected samples
  [5] Export and combine datasets
  [Q] Quit

Choice: 1

Collecting NORMAL data...
Duration: 30 seconds
[███████████████████████░░░░░░░░░░░░░░░░] 20/30s | Samples: 20

✓ Collected 30 samples of NORMAL
```

### Generated Files

The tool saves data to `training_data/` directory:

- `training_data_normal.csv` - Normal behavior samples
- `training_data_vibration.csv` - Vibration/movement samples
- `training_data_tampering.csv` - Tampering/attack samples
- `training_data_combined.csv` - All samples combined (for model training)

CSV columns: `timestamp, label, x_mean, x_var, x_rms, x_peak, y_mean, ...z_peak`

### Data Collection Tips

**For NORMAL scenario:**
- Keep device stationary on a stable surface
- Avoid vibrations from nearby machinery
- Collect 2-3 separate sessions (~30 sec each = 30 samples) for robustness

**For VIBRATION scenario:**
- Gently shake the device back-and-forth
- Simulate normal vibrations from nearby objects
- Don't shake too aggressively (that's TAMPERING)

**For TAMPERING scenario:**
- Rapidly shake, rotate, or attempt to open the device
- Try to trigger false positives if possible (model should learn the difference)
- Collect multiple attack patterns

**General Tips:**
- Aim for 100+ samples total per scenario (33 samples/category minimum)
- More data = better model generalization
- Mix multiple sessions/time periods if possible
- Export intermediate datasets to avoid data loss

---

## 2. Training the Anomaly Detection Model

### Prerequisites

```bash
pip3 install scikit-learn pandas numpy matplotlib seaborn
```

### Usage

```bash
python3 train_anomaly_model.py \
    --data training_data/training_data_combined.csv \
    --algorithm isolation_forest \
    --output models/ \
    --contamination 0.15
```

### Algorithm Choices

#### Isolation Forest (Recommended)
- **Pros**: Fast, interpretable, works well with high-dimensional data
- **Cons**: Less sophisticated than deep learning
- **Best for**: Embedded systems with limited resources
- **Memory**: ~50-100 KB for trained model

```bash
python3 train_anomaly_model.py --data training_data/training_data_combined.csv \
                               --algorithm isolation_forest \
                               --contamination 0.15
```

#### One-Class SVM
- **Pros**: Good at identifying outliers, handles complex boundaries
- **Cons**: Slower inference, requires careful hyperparameter tuning
- **Best for**: Well-separated normal/anomaly clusters
- **Memory**: ~100-200 KB

```bash
python3 train_anomaly_model.py --data training_data/training_data_combined.csv \
                               --algorithm one_class_svm \
                               --contamination 0.10
```

### Output Metrics

```
==================================================
Accuracy                      85.5%
Precision                     81.2%  (minimize false positives)
Recall                        92.3%  (catch real anomalies)
F1 Score                      0.866
ROC-AUC                       0.923
==================================================

Confusion Matrix:
  True Negatives:              42  (correct: normal)
  False Positives:             10  (false alarms)
  False Negatives:              3  (missed anomalies)
  True Positives:              35  (correct: anomaly)
```

### Interpreting Results

| Metric | Meaning | Target |
|--------|---------|--------|
| **Accuracy** | % correct predictions | >85% |
| **Precision** | % of flagged anomalies that are real | >80% (avoid false alarms) |
| **Recall** | % of real anomalies caught | >90% (catch threats) |
| **F1 Score** | Balance between precision & recall | >0.85 |
| **ROC-AUC** | Overall discrimination ability | >0.90 |

### Success Criteria

Model is ready for deployment if:
- ✅ Recall > 90% (catch real anomalies)
- ✅ Precision > 80% (not too many false alarms)
- ✅ Accuracy > 85%

If metrics are poor:
1. Collect more training data (especially underrepresented classes)
2. Try different algorithms
3. Adjust contamination parameter
4. Check for data quality issues

### Generated Artifacts

```
models/
├── isolation_forest_20260305_153245.pkl          (trained model)
├── isolation_forest_20260305_153245_scaler.pkl   (feature normalizer)
└── isolation_forest_20260305_153245_metrics.json (performance metrics)
```

---

## 3. Export Trained Model to Embedded C

Use the exporter to generate deployable C artifacts directly from sklearn model + scaler:

```bash
python3 export_iforest_to_c.py \
  --model models/isolation_forest.pkl \
  --scaler models/isolation_forest_scaler.pkl \
  --calibration-data training_data/training_data_combined.csv \
  --project-root ..
```

Generated files:
- `Core/Inc/generated_iforest_model.h`
- `Core/Src/generated_iforest_model.c`

Notes:
- The exporter applies scaler mean/scale and emits tree arrays for all estimators.
- It calibrates a default threshold from labeled data (best F1).
- Runtime now uses this generated backend through `ml_model.c`.

TensorFlow Lite conversion remains optional for future work if you prefer a TFLM backend.

---

## 4. Deployment Checklist

After training is complete:

- [ ] Review model metrics (Recall >90%, Precision >80%)
- [ ] Export trained model to embedded C (`export_iforest_to_c.py`)
- [ ] Verify generated model build and flash on target
- [ ] Test inference timing on device (<50ms budget)
- [ ] Validate with real-world normal/anomaly scenarios
- [ ] Adjust anomaly threshold if needed
- [ ] integrate ML scores into FSM decision logic

---

## Troubleshooting

### No data collected / serial connection fails

```bash
# Check COM port availability
# Windows: Device Manager → Ports (COM & LPT)
# Linux: ls /dev/ttyUSB*

# Verify board is running and outputting [FE] lines
# Use any serial terminal (PuTTY, minicom, Arduino IDE, etc.) to see raw output
```

### Poor model performance (low recall or precision)

1. **Insufficient data**: Collect more samples (aim for 100+ per class)
2. **Imbalanced classes**: Ensure similar sample counts for each scenario
3. **Feature quality**: Verify accelerometer is calibrated properly
4. **Algorithm mismatch**: Try both Isolation Forest and One-Class SVM
5. **Hyperparameters**: Adjust contamination parameter

### Model too large for device

- Keep model size <30 KB (embedded budget is 46 KB)
- Prefer Isolation Forest over One-Class SVM
- Use TensorFlow Lite quantization (int8)
- Reduce number of decision trees if necessary

---

## References

- **Isolation Forest**: Liu et al. (2008) "Isolation Forest"
- **One-Class SVM**: Schölkopf et al. (1999)
- **Feature Extraction**: See `Core/Inc/feature_extraction.h`
- **ML API**: See `Core/Inc/ml_model.h`
- **Device Constraints**: STM32H563ZI with 2MB flash, 250KB SRAM
