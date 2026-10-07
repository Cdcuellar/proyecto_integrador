const express = require("express");
const path = require("path");

const app = express();
const PORT = 3000;
const MAX_REGISTROS = 1000;

app.use(express.json());

// Sirve la página web de la carpeta "public" en la raíz (/)
app.use(express.static(path.join(__dirname, "public")));

// Por ahora los datos se guardan en memoria.
// Más adelante este arreglo se reemplaza por MongoDB.
const datos = [];

// Log de cada petición
app.use((req, res, next) => {
  console.log(`${new Date().toISOString()}  ${req.method} ${req.url}`);
  next();
});

// Prueba rápida: ¿el servidor está vivo?
app.get("/api/health", (req, res) => {
  res.json({ ok: true, registros: datos.length });
});

// La ESP32 envía aquí sus datos
app.post("/api/datos", (req, res) => {
  const registro = { ...req.body, recibido: new Date().toISOString() };
  datos.push(registro);
  if (datos.length > MAX_REGISTROS) datos.shift();

  console.log("Dato recibido:", registro);
  res.status(201).json({ ok: true });
});

// Consultar los últimos datos: /api/datos?limite=20
app.get("/api/datos", (req, res) => {
  const limite = Math.min(parseInt(req.query.limite) || 50, MAX_REGISTROS);
  res.json(datos.slice(-limite).reverse());
});

app.listen(PORT, "0.0.0.0", () => {
  console.log(`Servidor escuchando en el puerto ${PORT}`);
});
