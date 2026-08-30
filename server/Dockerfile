FROM python:3.13-slim

WORKDIR /srv

# Dependencies are pure-Python (PyMySQL) or have aarch64 wheels, so no
# build toolchain is needed on the Pi.
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Applied at startup by db.ensure_schema(); resolved as /srv/schema.sql.
COPY schema.sql .
COPY app ./app

EXPOSE 9101

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "9101", "--log-level", "info"]
