from machine import I2C

class INA219:
    # Registros internos del chip
    REG_CONFIG = 0x00
    REG_SHUNTVOLTAGE = 0x01
    REG_BUSVOLTAGE = 0x02
    REG_POWER = 0x03
    REG_CURRENT = 0x04
    REG_CALIBRATION = 0x05

    def __init__(self, i2c, addr=0x40):
        """ Inicializa el sensor con el bus I2C ya creado """
        self.i2c = i2c
        self.addr = addr

    def _write_register(self, reg, value):
        # Escribe 16 bits en el registro indicado
        self.i2c.writeto_mem(self.addr, reg, bytearray([(value >> 8) & 0xFF, value & 0xFF]))

    def _read_register(self, reg):
        # Lee 16 bits del registro indicado
        data = self.i2c.readfrom_mem(self.addr, reg, 2)
        return (data[0] << 8) | data[1]

    def configure(self):
        """
        Calibración para un Shunt de 0.1 Ohmios.
        Rango de 32V, Gain /8 (320mV).
        Hardware averaging de 128 muestras (68.1 ms) para filtrar ripple de PWM a 500 Hz.
        """
        # 1. Enviar valor de calibración (4096 para esta configuración)
        self._write_register(self.REG_CALIBRATION, 4096)
        
        # 2. Configurar el registro principal:
        # 0x3FFF = 32V, Gain /8, BADC 128 samples (68 ms), SADC 128 samples (68 ms), Shunt & Bus continuo
        self._write_register(self.REG_CONFIG, 0x3FFF)

    def current(self):
        """
        Retorna la corriente leída en miliAmperios (mA).
        Se lee directamente del registro de tensión del Shunt (REG_SHUNTVOLTAGE).
        Esto evita lecturas a 0 mA si un pico de corriente reinicia el registro de calibración volátil del chip.
        Con shunt de 0.1 Ohm y LSB de 10 uV: I [mA] = Vshunt [mV] / 0.1 = val * 0.01 / 0.1 = val / 10.0
        """
        val = self._read_register(self.REG_SHUNTVOLTAGE)
        
        # Corrección para números negativos (complemento a 2 de 16 bits)
        if val > 32767:
            val -= 65536
            
        return val / 10.0

    def voltage(self):
        """ Retorna la tensión del bus en Voltios (V) """
        val = self._read_register(self.REG_BUSVOLTAGE)
        # Se descartan los 3 bits menos significativos (shift) y se multiplica por LSB de 4mV
        val = (val >> 3) * 4
        return val / 1000.0
