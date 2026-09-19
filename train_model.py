from sklearn.ensemble import RandomForestClassifier
import joblib
import pandas as pd

# จำลองข้อมูลเซ็นเซอร์สำหรับเทรน (ถ้ามีข้อมูลจริงเอามาใส่ตรงนี้)
data = {
    'N': [90, 85, 60, 20, 10, 80],
    'P': [42, 58, 55, 30, 15, 50],
    'K': [43, 41, 44, 20, 10, 45],
    'temperature': [20.8, 21.7, 23.0, 25.0, 30.0, 22.0],
    'humidity': [82.0, 80.3, 85.0, 40.0, 35.0, 75.0],
    'crop': ['ข้าว', 'ข้าว', 'ข้าวโพด', 'ถั่ว', 'พริก', 'มะเขือเทศ']
}
df = pd.DataFrame(data)

X = df[['N', 'P', 'K', 'temperature', 'humidity']]
y = df['crop']

model = RandomForestClassifier(n_estimators=100, random_state=42)
model.fit(X, y)

joblib.dump(model, 'crop_model.joblib')
print("สร้างไฟล์สมองกล crop_model.joblib สำเร็จ!")