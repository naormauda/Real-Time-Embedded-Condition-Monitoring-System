# Phase 3b Quick-Start Execution Guide

## Prerequisites Checklist

Before starting data collection, verify:

- [ ] NUCLEO-H563ZI board is connected via USB
- [ ] Latest firmware flashed to the board (includes feature extraction)
- [ ] Python 3.x installed on your PC
- [ ] pyserial installed: `pip3 install pyserial`
- [ ] Serial COM port identified (see below)

---

## Step 1: Identify Your COM Port

### Windows:
1. Open Device Manager
2. Expand "Ports (COM & LPT)"
3. Look for "STMicroelectronics Virtual COM Port (COMX)"
4. Note the COM number (e.g., COM3, COM5, etc.)

### Linux/Mac:
```bash
ls /dev/tty*
# Look for /dev/ttyUSB0 or /dev/ttyACM0
```

---

## Step 2: Verify Board is Running

Open any serial terminal (PuTTY, Arduino IDE Serial Monitor, etc.):
- Port: Your COM port
- Baud rate: 115200
- Data bits: 8, Stop bits: 1, Parity: None

**Expected output every ~1 second:**
```
[FE] FE: X[u=0.5 v=1.2 r=1.3 p=5.0] Y[u=-0.2 v=0.2 r=0.3 p=0.8] Z[...]
[ML] ML: score=0.12 anomaly=NO  conf=0.88 time=5ms
```

If you see these lines → ✅ **Ready for data collection!**

If not → Check firmware is flashed, board has power, correct COM port

---

## Step 3: Collect Training Data

```bash
cd d:\project_real_time_embedded\smart_safe\tools
python3 collect_training_data.py --port COM3 --baudrate 115200
```

(Replace COM3 with your actual port)

### Interactive Menu:

**Option 1: NORMAL Scenario (30 seconds)**
- Keep device completely stationary
- Place on stable surface away from vibrations
- Do NOT touch or move during collection
- Press [1], wait for 30 samples to collect

**Option 2: VIBRATION Scenario (30 seconds)**
- Gently shake device back and forth
- Simulate environmental vibrations (e.g., nearby footsteps)
- Maintain consistent gentle motion
- Press [2], shake continuously for 30 seconds

**Option 3: TAMPERING Scenario (30 seconds)**
- Rapidly shake, rotate, or attempt to "break in"
- Simulate aggressive physical attack
- Mix different motion patterns (shake, twist, drop)
- Press [3], tamper aggressively for 30 seconds

**Option 4: View Statistics**
- Check how many samples collected per scenario
- Verify balanced dataset (aim for similar counts)

**Option 5: Export Data**
- Saves to `training_data/` directory
- Creates individual CSVs + combined dataset

**Press Q to Quit**

### Recommended Collection Strategy:

For best results, collect data in this order:

1. **NORMAL** (1x session = 30 samples)
2. **VIBRATION** (1x session = 30 samples)
3. **TAMPERING** (1x session = 30 samples)
4. View statistics (should see ~30 samples each)
5. Export datasets
6. **REPEAT** (collect 2-3 sessions per scenario for robustness)
   - More data → better model generalization
   - Target: 90-150 total samples (30-50 per class)

**Tip:** Collect at different times of day to capture sensor drift

---

## Step 4: Train the Model

After exporting training data:

```bash
cd d:\project_real_time_embedded\smart_safe\tools
python3 train_anomaly_model.py \
    --data training_data/training_data_combined.csv \
    --algorithm isolation_forest \
    --contamination 0.15
```

**Expected output:**
```
Loading data from training_data/training_data_combined.csv...
Dataset shape: (90, 13)
Label distribution:
NORMAL        30
VIBRATION     30
TAMPERING     30

Training Isolation Forest (contamination=0.15)...

==================================================
Accuracy                      87.8%
Precision                     83.3%  (minimize false positives)
Recall                        93.3%  (catch real anomalies)
F1 Score                      0.880
ROC-AUC                       0.921
==================================================

✓ Training complete

✓ Model saved: models/isolation_forest_20260307_143022.pkl
✓ Scaler saved: models/isolation_forest_20260307_143022_scaler.pkl
✓ Metrics saved: models/isolation_forest_20260307_143022_metrics.json
```

---

## Step 5: Evaluate Results

### Success Criteria:

| Metric | Target | Meaning |
|--------|--------|---------|
| **Recall** | >90% | Catch 90%+ of real anomalies (security critical) |
| **Accuracy** | >85% | Overall correct predictions |
| **Precision** | >80% | Minimize false alarms |

**If metrics are GOOD:**
✅ Proceed to Phase 3c (TensorFlow Lite conversion & deployment)

**If metrics are POOR:**
1. Collect more training data (100+ samples)
2. Ensure balanced class distribution
3. Try `--algorithm one_class_svm`
4. Adjust contamination parameter (0.10 - 0.20)
5. Check accelerometer calibration

---

## Step 6: Review Saved Artifacts

Check these files were created:

```
training_data/
├── training_data_normal.csv      (NORMAL samples only)
├── training_data_vibration.csv   (VIBRATION samples only)
├── training_data_tampering.csv   (TAMPERING samples only)
└── training_data_combined.csv    (all samples - used for training)

models/
├── isolation_forest_*.pkl         (trained model)
├── isolation_forest_*_scaler.pkl  (feature normalizer)
└── isolation_forest_*_metrics.json (performance report)
```

---

## Troubleshooting

### "SerialException: could not open port"
- Check COM port number (Device Manager)
- Close other programs using the serial port
- Reconnect USB cable
- Try a different USB port

### "No feature output detected"
- Firmware might not be running
- Check ST-LINK connection
- Reflash firmware: `ninja` then upload .bin
- Verify board has power (LED should be on)

### "Poor model accuracy (<80%)"
- Collect more data (aim for 100+ samples total)
- Ensure balanced classes (similar counts per scenario)
- Try different algorithm: `--algorithm one_class_svm`
- Check if motion patterns are distinct enough

### "Model file size too large"
- Use Isolation Forest (smaller than SVM)
- Reduce contamination parameter
- Quantize model in Phase 3c (TensorFlow Lite)

---

## Next Phase (3c): TensorFlow Lite Deployment

After successful training, Phase 3c will:
1. Convert sklearn model → TensorFlow Lite format
2. Generate C code for STM32 embedding
3. Replace ml_model.c stub with real inference
4. Integrate ML scores into FSM
5. Validate on hardware

**But first:** Execute Steps 1-6 above to get trained model! 🎯

---

## Questions?

See `tools/README.md` for detailed documentation.

**Ready to start?**
1. Verify board is connected and running
2. Identify COM port
3. Run collect_training_data.py
4. Follow interactive prompts
5. Export and train!
