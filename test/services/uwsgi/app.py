from flask import Flask, request, jsonify

app = Flask(__name__)

@app.route("/")
def main():
   response = {
      "service": "uwsgi",
      "headers": dict(request.headers)
   }
   print(response)
   return jsonify(response)
