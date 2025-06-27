#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

// Enumeraciones para estados del sistema
typedef enum {
    ESTADO_INICIAL,
    ESTADO_CERRANDO,
    ESTADO_ABRIENDO,
    ESTADO_CERRADO,
    ESTADO_ABIERTO,
    ESTADO_ERR,
    ESTADO_STOP
} Estado;

// Enumeraciones para errores
typedef enum {
    ERR_OK,
    ERR_OT,
    ERR_LSW
} CodigoError;

// Estructura de entradas/salidas físicas
struct IO {
    bool LSC;        // Sensor puerta cerrada
    bool LSA;        // Sensor puerta abierta
    bool BA;         // Botón abrir
    bool BC;         // Botón cerrar
    bool SE;         // Botón emergencia
    bool MA;         // Motor abrir
    bool MC;         // Motor cerrar
    bool lampara;    // Lámpara
    bool buzzer;     // Buzzer
    bool PP;         // Botón Push-Push
    unsigned int MQTT_CMD; // Comando remoto
} io;

// Variables de estado
struct STATUS {
    unsigned int cntTimerCA;
    unsigned int cntRunTimer;
    CodigoError ERR_COD;
} status = {0, 0, ERR_OK};

struct CONFIG {
    unsigned int RunTimer;
    unsigned int TimerCA;
} config = {180, 100};

Estado EstadoActual = ESTADO_INICIAL;
bool lampState = false;
bool prevPPState = false;

Estado Func_ESTADO_INICIAL(void) {
    status.cntRunTimer = 0;
    io.LSA = true;
    io.LSC = true;
    printf("==> Estado INICIAL\n");

    if (io.LSC && io.LSA) return ESTADO_ERR;
    if (io.LSC && !io.LSA) return ESTADO_CERRADO;
    if (!io.LSC && io.LSA) return ESTADO_CERRANDO;
    return ESTADO_STOP;
}

Estado Func_ESTADO_CERRANDO(void) {
    status.cntRunTimer = 0;
    io.MA = false;
    io.MC = true;
    printf("==> Estado CERRANDO\n");

    for (;;) {
        lampState = !lampState;
        io.lampara = lampState;
        io.buzzer = lampState;

        printf("LÁMPARA: %s\n", lampState ? "ENCENDIDA" : "APAGADA");
        printf("BUZZER: %s\n", lampState ? "SONANDO" : "SILENCIO");
        usleep(250000);

        if (io.LSC) return ESTADO_CERRADO;
        if (io.BA || io.BC || io.SE) return ESTADO_STOP;
        if (++status.cntRunTimer > config.RunTimer) {
            status.ERR_COD = ERR_OT;
            return ESTADO_ERR;
        }
    }
}

Estado Func_ESTADO_ABRIENDO(void) {
    status.cntRunTimer = 0;
    io.MA = true;
    io.MC = false;
    printf("==> Estado ABRIENDO\n");

    for (;;) {
        lampState = !lampState;
        io.lampara = lampState;
        io.buzzer = lampState;

        printf("LÁMPARA: %s\n", lampState ? "ENCENDIDA" : "APAGADA");
        printf("BUZZER: %s\n", lampState ? "SONANDO" : "SILENCIO");
        usleep(500000);

        if (io.LSA) return ESTADO_ABIERTO;
        if (io.BA || io.BC || io.SE) return ESTADO_STOP;
        if (++status.cntRunTimer > config.RunTimer) {
            status.ERR_COD = ERR_OT;
            return ESTADO_ERR;
        }
    }
}

Estado Func_ESTADO_CERRADO(void) {
    io.MA = false;
    io.MC = false;
    io.lampara = false;
    io.buzzer = false;
    printf("==> Estado CERRADO\n");

    for (;;) {
        if (io.BA || io.MQTT_CMD == 1 || (io.PP && !prevPPState)) {
            prevPPState = io.PP;
            return ESTADO_ABRIENDO;
        }
        prevPPState = io.PP;
    }
}

Estado Func_ESTADO_ABIERTO(void) {
    io.MA = false;
    io.MC = false;
    io.lampara = true;
    io.buzzer = false;
    status.cntTimerCA = 0;
    printf("==> Estado ABIERTO\n");

    for (;;) {
        printf("LÁMPARA: ENCENDIDA (puerta abierta)\n");
        sleep(1);
        if (++status.cntTimerCA > config.TimerCA || io.BC || io.MQTT_CMD == 2 || (io.PP && !prevPPState)) {
            prevPPState = io.PP;
            return ESTADO_CERRANDO;
        }
        prevPPState = io.PP;
    }
}

Estado Func_ESTADO_ERR(void) {
    io.MA = false;
    io.MC = false;
    io.lampara = false;
    io.buzzer = true;
    printf("==> Estado ERROR\n");

    bool mensajeMostrado = false;

    for (;;) {
        if (io.LSC && io.LSA) {
            status.ERR_COD = ERR_LSW;
            if (!mensajeMostrado) {
                printf("ERROR: Ambos sensores activos.\n");
                mensajeMostrado = true;
            }
        } else if (status.ERR_COD == ERR_LSW) {
            if ((io.LSC && !io.LSA) || (!io.LSC && io.LSA)) {
                status.ERR_COD = ERR_OK;
                return ESTADO_INICIAL;
            }
        } else if (status.ERR_COD == ERR_OT) {
            printf("ERROR: Tiempo excedido. Cerrando.\n");
            return ESTADO_CERRANDO;
        }
        sleep(1);
    }
}

Estado Func_ESTADO_STOP(void) {
    io.MA = false;
    io.MC = false;
    io.buzzer = false;
    io.lampara = false;
    printf("==> Estado STOP\n");

    for (;;) {
        if ((io.BA && !io.LSA) || io.MQTT_CMD == 1) return ESTADO_ABRIENDO;
        if ((io.BC && !io.LSC) || io.MQTT_CMD == 2) return ESTADO_CERRANDO;
        if (io.PP && !prevPPState) {
            prevPPState = io.PP;
            if (io.LSA) return ESTADO_CERRANDO;
            else if (io.LSC) return ESTADO_ABRIENDO;
            else return ESTADO_CERRANDO;
        }
        if (io.LSC && io.LSA) {
            status.ERR_COD = ERR_LSW;
            return ESTADO_ERR;
        }
        prevPPState = io.PP;
    }
}

int main() {
    Estado siguienteEstado = ESTADO_INICIAL;

    for (;;) {
        switch (siguienteEstado) {
            case ESTADO_INICIAL:
                siguienteEstado = Func_ESTADO_INICIAL(); break;
            case ESTADO_CERRANDO:
                siguienteEstado = Func_ESTADO_CERRANDO(); break;
            case ESTADO_ABRIENDO:
                siguienteEstado = Func_ESTADO_ABRIENDO(); break;
            case ESTADO_CERRADO:
                siguienteEstado = Func_ESTADO_CERRADO(); break;
            case ESTADO_ABIERTO:
                siguienteEstado = Func_ESTADO_ABIERTO(); break;
            case ESTADO_ERR:
                siguienteEstado = Func_ESTADO_ERR(); break;
            case ESTADO_STOP:
                siguienteEstado = Func_ESTADO_STOP(); break;
            default:
                siguienteEstado = ESTADO_ERR; break;
        }
    }
    return 0;
}
