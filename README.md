# 📡 Práctica 2 – Parte C  
## Medidor de Frecuencia con ESP32 (PlatformIO / Arduino)

---

## 📌 Descripción

En esta práctica se implementa un **medidor de frecuencia** utilizando una placa ESP32.  
El sistema mide el tiempo entre interrupciones de una señal digital, almacena los valores en una **cola circular** y calcula:

- Frecuencia máxima (**Fmax**)
- Frecuencia media (**Fmed**)
- Frecuencia mínima (**Fmin**)

Los resultados se muestran en una **página web** generada por la propia ESP32.

---

## 🎯 Objetivos

- Utilizar **interrupciones externas**
- Medir tiempo con **timer hardware**
- Implementar una **cola circular**
- Calcular estadísticas en tiempo real
- Crear un **servidor web embebido**

---

## ⚙️ Funcionamiento

1. Una señal externa genera interrupciones en un pin de la ESP32.
2. En cada interrupción:
   - Se mide el tiempo con un timer hardware.
   - Se calcula el periodo entre interrupciones.
   - Se almacena en una cola circular.
3. El programa principal:
   - Extrae los valores de la cola
   - Calcula Fmax, Fmed y Fmin
4. La ESP32 crea un punto de acceso WiFi y muestra los resultados en una página web.

---

## 🧠 Cálculo de frecuencia

La frecuencia se obtiene a partir del periodo:


f = 1 / T


En microsegundos:


f = 1.000.000 / T(us)


---

## 🔁 Cola circular

Se utiliza una estructura FIFO con:

- Tamaño fijo
- Índice de escritura (head)
- Índice de lectura (tail)
- Control de overflow

Permite gestionar datos de forma eficiente sin uso dinámico de memoria.

---

## 🌐 Servidor Web

La ESP32 crea un **Access Point (AP)**:

- **SSID:** ESP32_Frecuencimetro  
- **Password:** 12345678  

Acceso desde navegador:


http://192.168.4.1


La página muestra:

- Fmax
- Fmed
- Fmin
- Número de muestras
- Estado del sistema

---

## 🔌 Hardware utilizado

- ESP32-S3
- Señal digital de entrada (generador de funciones, otra placa, etc.)

---

## 📍 Configuración del pin

```cpp
static const uint8_t SIGNAL_PIN = 4;
