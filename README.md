# Laboratorio de Sistemas Incrustados IE0301

## Proyecto y Práctica 3

### Estructura del repositorio

- **IIParte/** y **IIIParte/**: Estas carpetas contienen el desarrollo correspondiente a la **Práctica 3**, donde se realizaron pruebas y ejercicios intermedios enfocados en el manejo de GStreamer y DeepStream, incluyendo la detección y seguimiento básico de objetos.

- **Proyecto/**: Carpeta que contiene la implementación final del **proyecto principal**, descrito abajo.

---

## Introducción

En el desarrollo de sistemas de vigilancia, es común realizar detección, clasificación y tracking de objetos.  
Por ejemplo, un sistema embebido con una cámara de seguridad inteligente instalada en la vía pública puede detectar personas y vehículos, así como dar seguimiento a la posición de cada objeto detectado y clasificar características como la marca, el color o el número de placa de los vehículos.

---

## Requerimientos

Este proyecto consiste en desarrollar una aplicación que reciba como entrada video desde una cámara o desde un archivo (solo uno a la vez).  
La aplicación debe:

- Realizar detección de objetos (personas y vehículos).
- Hacer tracking (seguimiento) de los objetos detectados.
- En caso de ser vehículos, detectar la **marca** del vehículo.
- Generar el video con los **bounding boxes** y las marcas detectadas.
- Renderizar el video simultáneamente en el display local.
- Grabar el video en un archivo.
- Transmitir el video por UDP a un servidor en la red.