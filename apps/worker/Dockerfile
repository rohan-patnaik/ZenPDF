FROM python:3.11-slim

WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY zenpdf_worker ./zenpdf_worker
COPY main.py ./main.py

CMD ["python", "main.py"]
