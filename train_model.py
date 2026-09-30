from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score
import joblib
import pandas as pd
import numpy as np
import os

# 1. โหลด Dataset 15 พืชที่สร้างจาก "ดาตาaiพืชตัวใหม่.txt"
dataset_path = os.path.join(os.path.dirname(__file__), 'greenmind_dataset_v2.csv')
if not os.path.exists(dataset_path):
    print("ไม่พบไฟล์ greenmind_dataset_v2.csv กรุณารัน generate_dataset.py ก่อน")
    exit(1)

df = pd.read_csv(dataset_path, encoding='utf-8-sig')

X = df[['N', 'P', 'K', 'temperature', 'humidity']].values
y = df['crop'].values

X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42, stratify=y)

# 2. เทรนโมเดล Random Forest Classifier
print("กำลังเทรนโมเดล Random Forest สำหรับ 15 ชนิดพืช...")
model = RandomForestClassifier(n_estimators=150, random_state=42)
model.fit(X_train, y_train)

# 3. ประเมินผลความแม่นยำ
y_pred = model.predict(X_test)
acc = accuracy_score(y_test, y_pred)
print(f"ความแม่นยำของโมเดล (Accuracy): {acc * 100:.2f}%")

# 4. เทรนโมเดลซ้ำด้วยข้อมูลทั้งหมดเพื่อให้ครอบคลุมที่สุด
model.fit(X, y)

# 5. บันทึกโมเดลลงไฟล์ crop_model.joblib
output_model_path = os.path.join(os.path.dirname(__file__), 'crop_model.joblib')
joblib.dump(model, output_model_path)
print(f"บันทึกไฟล์โมเดลสำเร็จที่: {output_model_path}")
print(f"รายชื่อพืชทั้งหมดในโมเดล ({len(model.classes_)} ชนิด):")
for idx, crop in enumerate(model.classes_, 1):
    print(f"  {idx}. {crop}")