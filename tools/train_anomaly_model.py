#!/usr/bin/env python3
"""
Anomaly Detection Model Training Script

This script uses collected training data to train an anomaly detection model
using multiple algorithms (Isolation Forest, One-Class SVM, Autoencoder).

The trained model can then be converted to TensorFlow Lite for deployment
on the NUCLEO-H563ZI board.

Usage:
    python3 train_anomaly_model.py --data training_data/training_data_combined.csv \
                                    --algorithm isolation_forest \
                                    --output models/

Output:
    - Trained model (.pkl file)
    - Performance metrics (accuracy, precision, recall, ROC-AUC)
    - Model evaluation plots
"""

import argparse
import sys
import pandas as pd
import numpy as np
from pathlib import Path
from datetime import datetime
import pickle
import json

try:
    from sklearn.ensemble import IsolationForest
    from sklearn.svm import OneClassSVM
    from sklearn.preprocessing import StandardScaler
    from sklearn.model_selection import train_test_split
    from sklearn.metrics import (
        confusion_matrix, classification_report, roc_auc_score,
        roc_curve, auc
    )
    import matplotlib.pyplot as plt
    import seaborn as sns
except ImportError as e:
    print(f"Error: Required packages not found. Install with:")
    print(f"  pip3 install scikit-learn pandas numpy matplotlib seaborn")
    sys.exit(1)


class AnomalyModelTrainer:
    """Trains anomaly detection models for Smart Safe"""

    def __init__(self, output_dir='models'):
        """
        Initialize trainer

        Args:
            output_dir: Directory to save trained models
        """
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.model = None
        self.scaler = None
        self.metrics = {}
        self.feature_names = [
            'x_mean', 'x_var', 'x_rms', 'x_peak',
            'y_mean', 'y_var', 'y_rms', 'y_peak',
            'z_mean', 'z_var', 'z_rms', 'z_peak'
        ]

    def load_data(self, csv_file):
        """
        Load training data from CSV

        Args:
            csv_file: Path to CSV file from collect_training_data.py

        Returns:
            Tuple of (features, labels)
        """
        print(f"Loading data from {csv_file}...")
        df = pd.read_csv(csv_file)

        print(f"Dataset shape: {df.shape}")
        print(f"Label distribution:\n{df['label'].value_counts()}\n")

        # Extract features and labels
        X = df[self.feature_names].values
        y = df['label'].values

        # Convert labels to binary (NORMAL=0, VIBRATION/TAMPERING=1)
        y_binary = (y != 'NORMAL').astype(int)

        return X, y, y_binary

    def train_isolation_forest(self, X, y_binary, contamination=0.1, fit_normal_only=True):
        """
        Train Isolation Forest anomaly detector

        Args:
            X: Feature matrix
            y_binary: Binary labels (0=normal, 1=anomaly)
            contamination: Expected proportion of anomalies
        """
        mode = "NORMAL-only" if fit_normal_only else "all-data"
        print(f"Training Isolation Forest (contamination={contamination}, mode={mode})...")

        # Normalize features
        self.scaler = StandardScaler()
        if fit_normal_only:
            X_train = X[y_binary == 0]
            if X_train.shape[0] == 0:
                raise ValueError("No NORMAL samples found. Cannot fit one-class anomaly model.")
            self.scaler.fit(X_train)
            X_train_scaled = self.scaler.transform(X_train)
        else:
            X_train_scaled = self.scaler.fit_transform(X)

        # Always evaluate on the full dataset
        X_scaled = self.scaler.transform(X)

        # Train model
        self.model = IsolationForest(
            contamination=contamination,
            random_state=42,
            n_estimators=100
        )
        self.model.fit(X_train_scaled)

        # Get predictions (anomaly_score: negative for anomalies, positive for normal)
        y_pred_score = -self.model.score_samples(X_scaled)  # Flip for intuitive range
        y_pred = self.model.predict(X_scaled)  # +1 for normal, -1 for anomaly
        y_pred_binary = (y_pred == -1).astype(int)  # Convert to 0/1

        # Evaluate
        self._evaluate(y_binary, y_pred_binary, y_pred_score)

        pred_anomaly_rate = float(np.mean(y_pred_binary))
        print(f"Predicted anomaly rate: {pred_anomaly_rate:.1%}")

        print(f"{Colors.OKGREEN}✓ Training complete{Colors.ENDC}\n")

    def train_one_class_svm(self, X, y_binary, nu=0.1, fit_normal_only=True):
        """
        Train One-Class SVM anomaly detector

        Args:
            X: Feature matrix
            y_binary: Binary labels
            nu: Upper bound on fraction of training data as outliers
        """
        mode = "NORMAL-only" if fit_normal_only else "all-data"
        print(f"Training One-Class SVM (nu={nu}, mode={mode})...")

        # Normalize features
        self.scaler = StandardScaler()
        if fit_normal_only:
            X_train = X[y_binary == 0]
            if X_train.shape[0] == 0:
                raise ValueError("No NORMAL samples found. Cannot fit one-class anomaly model.")
            self.scaler.fit(X_train)
            X_train_scaled = self.scaler.transform(X_train)
        else:
            X_train_scaled = self.scaler.fit_transform(X)

        # Always evaluate on the full dataset
        X_scaled = self.scaler.transform(X)

        # Train model
        self.model = OneClassSVM(kernel='rbf', gamma='auto', nu=nu)
        self.model.fit(X_train_scaled)

        # Get predictions
        y_pred = self.model.predict(X_scaled)  # +1 for normal, -1 for anomaly
        y_pred_binary = (y_pred == -1).astype(int)
        y_pred_score = -self.model.decision_function(X_scaled)  # Negative scores for anomalies

        # Evaluate
        self._evaluate(y_binary, y_pred_binary, y_pred_score)

        pred_anomaly_rate = float(np.mean(y_pred_binary))
        print(f"Predicted anomaly rate: {pred_anomaly_rate:.1%}")

        print(f"{Colors.OKGREEN}✓ Training complete{Colors.ENDC}\n")

    def _evaluate(self, y_true, y_pred, y_scores):
        """
        Evaluate model performance

        Args:
            y_true: True binary labels
            y_pred: Predicted binary labels
            y_scores: Anomaly scores (higher = more anomalous)
        """
        from sklearn.metrics import accuracy_score, precision_score, recall_score, f1_score

        accuracy = accuracy_score(y_true, y_pred)
        precision = precision_score(y_true, y_pred, zero_division=0)
        recall = recall_score(y_true, y_pred, zero_division=0)
        f1 = f1_score(y_true, y_pred, zero_division=0)

        # ROC-AUC
        try:
            roc_auc = roc_auc_score(y_true, y_scores)
        except:
            roc_auc = 0.0

        # Confusion matrix
        cm = confusion_matrix(y_true, y_pred)

        self.metrics = {
            'accuracy': float(accuracy),
            'precision': float(precision),
            'recall': float(recall),
            'f1_score': float(f1),
            'roc_auc': float(roc_auc),
            'confusion_matrix': cm.tolist(),
            'timestamp': datetime.now().isoformat()
        }

        # Print results
        print(f"\n{'='*50}")
        print(f"{'Accuracy':<20} {accuracy:>7.1%}")
        print(f"{'Precision':<20} {precision:>7.1%}  (minimize false positives)")
        print(f"{'Recall':<20} {recall:>7.1%}  (catch real anomalies)")
        print(f"{'F1 Score':<20} {f1:>7.3f}")
        print(f"{'ROC-AUC':<20} {roc_auc:>7.3f}")
        print(f"{'='*50}\n")

        print("Confusion Matrix:")
        print(f"  True Negatives:  {cm[0,0]:5d}  (correct: normal)")
        print(f"  False Positives: {cm[0,1]:5d}  (false alarms)")
        print(f"  False Negatives: {cm[1,0]:5d}  (missed anomalies)")
        print(f"  True Positives:  {cm[1,1]:5d}  (correct: anomaly)")
        print()

    def save_model(self, model_name=None):
        """
        Save trained model and scaler

        Args:
            model_name: Optional name for the model
        """
        if model_name is None:
            model_name = f"{self.model.__class__.__name__}_{datetime.now().strftime('%Y%m%d_%H%M%S')}"

        model_path = self.output_dir / f"{model_name}.pkl"
        scaler_path = self.output_dir / f"{model_name}_scaler.pkl"
        metrics_path = self.output_dir / f"{model_name}_metrics.json"

        # Save model
        with open(model_path, 'wb') as f:
            pickle.dump(self.model, f)
        print(f"{Colors.OKGREEN}✓{Colors.ENDC} Model saved: {model_path}")

        # Save scaler
        with open(scaler_path, 'wb') as f:
            pickle.dump(self.scaler, f)
        print(f"{Colors.OKGREEN}✓{Colors.ENDC} Scaler saved: {scaler_path}")

        # Save metrics
        with open(metrics_path, 'w') as f:
            json.dump(self.metrics, f, indent=2)
        print(f"{Colors.OKGREEN}✓{Colors.ENDC} Metrics saved: {metrics_path}\n")

        return model_path, scaler_path


class Colors:
    OKGREEN = '\033[92m'
    ENDC = '\033[0m'


def main():
    parser = argparse.ArgumentParser(
        description='Train anomaly detection model for Smart Safe'
    )
    parser.add_argument('--data', required=True, help='Training data CSV file')
    parser.add_argument('--algorithm', default='isolation_forest',
                       choices=['isolation_forest', 'one_class_svm'],
                       help='Training algorithm')
    parser.add_argument('--output', default='models', help='Output directory for models')
    parser.add_argument('--contamination', type=float, default=0.15,
                       help='Expected anomaly fraction (for Isolation Forest)')
    parser.add_argument('--fit-all-data', action='store_true',
                       help='Fit one-class model on all samples instead of NORMAL-only')

    args = parser.parse_args()

    trainer = AnomalyModelTrainer(args.output)

    # Load data
    X, y, y_binary = trainer.load_data(args.data)

    normal_ratio = float(np.mean(y_binary == 0))
    anomaly_ratio = float(np.mean(y_binary == 1))
    print(f"Normal ratio: {normal_ratio:.1%} | Anomaly ratio: {anomaly_ratio:.1%}")
    if anomaly_ratio > 0.5 and not args.fit_all_data:
        print("Dataset is anomaly-heavy; NORMAL-only fitting enabled (recommended for one-class models).")
    elif anomaly_ratio > 0.5 and args.fit_all_data:
        print("Warning: anomaly-heavy dataset + all-data fitting can severely reduce recall.")

    # Train model
    if args.algorithm == 'isolation_forest':
        trainer.train_isolation_forest(
            X,
            y_binary,
            contamination=args.contamination,
            fit_normal_only=(not args.fit_all_data)
        )
    elif args.algorithm == 'one_class_svm':
        trainer.train_one_class_svm(
            X,
            y_binary,
            nu=args.contamination,
            fit_normal_only=(not args.fit_all_data)
        )

    # Save model
    trainer.save_model(args.algorithm)

    print(f"\n{Colors.OKGREEN}Training complete!{Colors.ENDC}")
    print(f"Next step: Convert model to TensorFlow Lite for embedded deployment")


if __name__ == '__main__':
    main()
