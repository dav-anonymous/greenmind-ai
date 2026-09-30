from flask import Flask, request, jsonify
import joblib
import os

app = Flask(__name__)
model_path = os.path.join(os.path.dirname(__file__), 'crop_model.joblib')
model = joblib.load(model_path)

@app.after_request
def allow_prediction_from_browser(response):
    # The dashboard also opens directly from file:// (Origin: null).
    # Prediction is a public endpoint and does not use browser credentials.
    if request.path == '/predict':
        response.headers['Access-Control-Allow-Origin'] = '*'
        response.headers['Access-Control-Allow-Methods'] = 'POST, OPTIONS'
        response.headers['Access-Control-Allow-Headers'] = 'Content-Type'
    return response

@app.route('/predict', methods=['POST'])
def predict():
    try:
        data = request.get_json()
        features = [[
            float(data['N']), float(data['P']), float(data['K']), 
            float(data['temperature']), float(data['humidity'])
        ]]
        prediction = model.predict(features)
        return jsonify({"status": "success", "recommended_crop": str(prediction[0])})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)
