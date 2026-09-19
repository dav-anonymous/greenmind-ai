from sklearn.ensemble import RandomForestClassifier
import joblib
import pandas as pd
import numpy as np

# สเปกพื้นฐานของพืชทั้ง 20 ชนิด [N, P, K, Temp, Humidity]
crops_base = {
    'ผักบุ้งจีน': [50, 30, 40, 31.0, 78.0],
    'กวางตุ้ง': [60, 40, 50, 28.5, 72.0],
    'คะน้า': [80, 45, 60, 27.0, 78.0],
    'ผักกาดขาว': [70, 40, 55, 25.0, 70.0],
    'ผักชี': [40, 35, 35, 24.0, 65.0],
    'ต้นหอม': [55, 30, 45, 26.0, 68.0],
    'กะเพรา': [35, 25, 30, 31.5, 58.0],
    'โหระพา': [35, 25, 30, 30.0, 62.0],
    'แมงลัก': [30, 20, 25, 30.5, 57.0],
    'สะระแหน่': [45, 30, 40, 25.0, 72.0],
    'พริกขี้หนู': [20, 15, 20, 29.0, 52.0],
    'มะเขือเทศ': [75, 50, 65, 25.0, 68.0],
    'แตงกวา': [55, 40, 50, 29.0, 72.0],
    'ถั่วฝักยาว': [25, 35, 30, 28.5, 58.0],
    'ข้าวโพดหวาน': [90, 55, 60, 28.0, 72.0],
    'กรีนโอ๊ค': [50, 35, 45, 23.0, 65.0],
    'เรดโอ๊ค': [50, 35, 45, 22.5, 66.0],
    'คื่นช่าย': [65, 45, 55, 23.5, 78.0],
    'กะหล่ำปลี': [85, 50, 70, 21.5, 72.0],
    'ข้าว': [90, 45, 45, 28.0, 88.0]
}

# สร้างชุดข้อมูลแบบกระจายตัว (Augment) ชนิดละ 100 ตัวอย่าง เพื่อให้โมเดลแยกแยะได้แม่นยำ
np.random.seed(42)
rows = []

for crop_name, values in crops_base.items():
    n_base, p_base, k_base, temp_base, hum_base = values
    for _ in range(100):
        n = max(0, n_base + np.random.normal(0, 4))
        p = max(0, p_base + np.random.normal(0, 3))
        k = max(0, k_base + np.random.normal(0, 4))
        t = max(15.0, temp_base + np.random.normal(0, 1.5))
        h = np.clip(hum_base + np.random.normal(0, 3.5), 20.0, 100.0)
        rows.append([n, p, k, t, h, crop_name])

df = pd.DataFrame(rows, columns=['N', 'P', 'K', 'temperature', 'humidity', 'crop'])

X = df[['N', 'P', 'K', 'temperature', 'humidity']]
y = df['crop']

# เทรนโมเดล Random Forest
model = RandomForestClassifier(n_estimators=150, random_state=42)
model.fit(X, y)

# เซฟทับไฟล์โมเดลเดิม
joblib.dump(model, 'crop_model.joblib')
print("เทรนและสร้างไฟล์โมเดล 20 พืชสำเร็จ!")