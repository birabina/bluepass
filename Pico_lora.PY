"""
pico_lora.py — Firmware MicroPython para Raspberry Pi Pico
BluePASS — Receptor LoRa / Controlador de Trava Remota

Pinout SX1278 no Pico:
   GP2  → SCK  (SPI0)
   GP4  → MISO (SPI0)
   GP3  → MOSI (SPI0)
   GP5  → NSS  (CS manual)
   GP6  → RESET
   GP7  → DIO0 (IRQ: RxDone/TxDone)
   GP15 → RELÉ / TRAVA (HIGH = abre, LOW = fecha)

Protocolo LoRa (ASCII, terminado em '\\n'):
   RX (recebe do STM32):  "UID:XX:XX:XX:XX\\n"
   TX (envia ao STM32):   "ACK:GRANT\\n" ou "ACK:DENY\\n"

GUI: Pico envia status via USB serial (/dev/ttyACM0) sem TTL externo.
"""

from machine import Pin, SPI
import time
import sys

# ─────────────────────────────────────────────────────────────────────────────
# Configurações do sistema
# ─────────────────────────────────────────────────────────────────────────────

# UID mestre autorizado (deve ser igual ao do STM32)
MASTER_UID = "12:34:56:78"

# Tempo (ms) que a trava permanece aberta após acesso liberado
UNLOCK_TIME_MS = 3000

# Frequência LoRa — deve ser igual à do STM32
LORA_FREQ = 433_000_000

# ─────────────────────────────────────────────────────────────────────────────
# Registradores do SX1278 (idênticos ao driver C)
# ─────────────────────────────────────────────────────────────────────────────
REG_FIFO                = 0x00
REG_OP_MODE             = 0x01
REG_FRF_MSB             = 0x06
REG_FRF_MID             = 0x07
REG_FRF_LSB             = 0x08
REG_PA_CONFIG           = 0x09
REG_OCP                 = 0x0B
REG_LNA                 = 0x0C
REG_FIFO_ADDR_PTR       = 0x0D
REG_FIFO_TX_BASE_ADDR   = 0x0E
REG_FIFO_RX_BASE_ADDR   = 0x0F
REG_FIFO_RX_CURRENT     = 0x10
REG_IRQ_FLAGS           = 0x12
REG_RX_NB_BYTES         = 0x13
REG_PKT_RSSI_VALUE      = 0x1A
REG_MODEM_CONFIG_1      = 0x1D
REG_MODEM_CONFIG_2      = 0x1E
REG_PREAMBLE_MSB        = 0x20
REG_PREAMBLE_LSB        = 0x21
REG_PAYLOAD_LENGTH      = 0x22
REG_MODEM_CONFIG_3      = 0x26
REG_DETECTION_OPTIMIZE  = 0x31
REG_DETECTION_THRESHOLD = 0x37
REG_SYNC_WORD           = 0x39
REG_DIO_MAPPING_1       = 0x40
REG_VERSION             = 0x42
REG_PA_DAC              = 0x4D

MODE_LONG_RANGE  = 0x80
MODE_SLEEP       = 0x00
MODE_STDBY       = 0x01
MODE_TX          = 0x03
MODE_RX_CONT     = 0x05

IRQ_RX_DONE      = 0x40
IRQ_TX_DONE      = 0x08
IRQ_CRC_ERR      = 0x20

# ─────────────────────────────────────────────────────────────────────────────
# Classe SX1278
# ─────────────────────────────────────────────────────────────────────────────
class SX1278:
    def __init__(self):
        # SPI0: SCK=GP2, MOSI=GP3, MISO=GP4
        self.spi = SPI(0,
                       baudrate=5_000_000,
                       polarity=0,
                       phase=0,
                       sck=Pin(2),
                       mosi=Pin(3),
                       miso=Pin(4))

        self.cs   = Pin(5,  Pin.OUT, value=1)   # CS ativo em LOW
        self.rst  = Pin(6,  Pin.OUT, value=1)   # RESET ativo em LOW
        self.dio0 = Pin(7,  Pin.IN)             # DIO0: RxDone / TxDone

    # ── Acesso a registradores ────────────────────────────────────────────────
    def write_reg(self, reg, val):
        self.cs.low()
        self.spi.write(bytes([reg | 0x80, val]))
        self.cs.high()

    def read_reg(self, reg):
        self.cs.low()
        self.spi.write(bytes([reg & 0x7F]))
        result = self.spi.read(1)
        self.cs.high()
        return result[0]

    def set_mode(self, mode):
        self.write_reg(REG_OP_MODE, MODE_LONG_RANGE | mode)

    # ── Inicialização ─────────────────────────────────────────────────────────
    def init(self):
        # Reset hardware
        self.rst.low()
        time.sleep_ms(1)
        self.rst.high()
        time.sleep_ms(10)

        # Verifica versão
        ver = self.read_reg(REG_VERSION)
        if ver != 0x12:
            raise RuntimeError(f"SX1278 não detectado. VERSION={hex(ver)}")

        # Entra em sleep para mudar para LoRa mode
        self.set_mode(MODE_SLEEP)
        time.sleep_ms(2)

        # Frequência: 433 MHz
        # Frf = (433e6 * 2^19) / 32e6 = 0x6C8000
        frf = int((LORA_FREQ * (1 << 19)) / 32_000_000)
        self.write_reg(REG_FRF_MSB, (frf >> 16) & 0xFF)
        self.write_reg(REG_FRF_MID, (frf >>  8) & 0xFF)
        self.write_reg(REG_FRF_LSB, (frf >>  0) & 0xFF)

        # PA_BOOST 17 dBm
        self.write_reg(REG_PA_CONFIG, 0x8F)
        self.write_reg(REG_PA_DAC,    0x87)
        self.write_reg(REG_OCP,       0x3B)

        # LNA máximo
        self.write_reg(REG_LNA, 0x23)

        # BW=125kHz, CR=4/5, SF=7 — IDÊNTICO ao STM32
        self.write_reg(REG_MODEM_CONFIG_1, 0x72)
        self.write_reg(REG_MODEM_CONFIG_2, 0x74)
        self.write_reg(REG_MODEM_CONFIG_3, 0x04)

        # Preâmbulo 8 símbolos
        self.write_reg(REG_PREAMBLE_MSB, 0x00)
        self.write_reg(REG_PREAMBLE_LSB, 0x08)

        # Sync word privado (= STM32)
        self.write_reg(REG_SYNC_WORD, 0x12)

        # FIFO base addresses
        self.write_reg(REG_FIFO_TX_BASE_ADDR, 0x00)
        self.write_reg(REG_FIFO_RX_BASE_ADDR, 0x00)

        # Otimização de detecção SF7
        self.write_reg(REG_DETECTION_OPTIMIZE,  0xC3)
        self.write_reg(REG_DETECTION_THRESHOLD, 0x0A)

        # DIO0 → RxDone
        self.write_reg(REG_DIO_MAPPING_1, 0x00)

        # Standby
        self.set_mode(MODE_STDBY)
        time.sleep_ms(2)

        return True

    # ── Recepção ──────────────────────────────────────────────────────────────
    def start_rx(self):
        """Coloca em modo RX contínuo."""
        self.write_reg(REG_DIO_MAPPING_1, 0x00)  # DIO0 = RxDone
        self.write_reg(REG_FIFO_RX_BASE_ADDR, 0x00)
        self.write_reg(REG_FIFO_ADDR_PTR,     0x00)
        self.write_reg(REG_IRQ_FLAGS,          0xFF) # limpa flags
        self.set_mode(MODE_RX_CONT)

    def receive(self):
        """
        Tenta receber um pacote. Não-bloqueante.
        Retorna (bytes, rssi_dBm) ou (None, None) se nenhum pacote disponível.
        """
        if not self.dio0.value():
            return None, None

        flags = self.read_reg(REG_IRQ_FLAGS)
        self.write_reg(REG_IRQ_FLAGS, 0xFF)  # limpa

        if flags & IRQ_CRC_ERR:
            return None, None  # descarta pacote com CRC inválido

        nb = self.read_reg(REG_RX_NB_BYTES)
        if nb == 0:
            return None, None

        # Aponta FIFO para onde o pacote chegou
        self.write_reg(REG_FIFO_ADDR_PTR,
                       self.read_reg(REG_FIFO_RX_CURRENT))

        # Lê os bytes
        data = bytearray(nb)
        for i in range(nb):
            data[i] = self.read_reg(REG_FIFO)

        # RSSI
        rssi = -164 + self.read_reg(REG_PKT_RSSI_VALUE)

        return bytes(data), rssi

    # ── Transmissão ───────────────────────────────────────────────────────────
    def transmit(self, data: bytes, timeout_ms=2000):
        """Transmite bytes. Bloqueante até TxDone ou timeout."""
        if len(data) > 64:
            data = data[:64]

        self.set_mode(MODE_STDBY)
        self.write_reg(REG_DIO_MAPPING_1,  0x40)  # DIO0 = TxDone
        self.write_reg(REG_FIFO_ADDR_PTR,  0x00)
        self.write_reg(REG_PAYLOAD_LENGTH, len(data))

        for b in data:
            self.write_reg(REG_FIFO, b)

        self.write_reg(REG_IRQ_FLAGS, 0xFF)
        self.set_mode(MODE_TX)

        t0 = time.ticks_ms()
        while not self.dio0.value():
            if time.ticks_diff(time.ticks_ms(), t0) > timeout_ms:
                self.set_mode(MODE_STDBY)
                return False

        self.write_reg(REG_IRQ_FLAGS, IRQ_TX_DONE)
        self.set_mode(MODE_STDBY)
        return True


# ─────────────────────────────────────────────────────────────────────────────
# Controle da trava / relé
# ─────────────────────────────────────────────────────────────────────────────
class Trava:
    def __init__(self, pin_num=15):
        self.pin = Pin(pin_num, Pin.OUT, value=0)  # LOW = fechado por padrão

    def abrir(self):
        self.pin.high()

    def fechar(self):
        self.pin.low()

    @property
    def aberta(self):
        return self.pin.value() == 1


# ─────────────────────────────────────────────────────────────────────────────
# Log via USB serial (aparece como /dev/ttyACM0 no Linux, sem TTL externo)
# ─────────────────────────────────────────────────────────────────────────────
def log(msg):
    """Envia linha para a GUI via USB serial."""
    print(msg)  # sys.stdout já é a USB serial no Pico


def log_gui(event, uid="", rssi=0, granted=False):
    """
    Formato compatível com o SerialBackend da GUI Linux:
      "RFID:UID=XX:XX:XX:XX:GRANT\\n" ou ":DENY\\n"
    """
    result = "GRANT" if granted else "DENY"
    print(f"RFID:UID={uid}:{result}")


# ─────────────────────────────────────────────────────────────────────────────
# Processa um pacote recebido do STM32
# ─────────────────────────────────────────────────────────────────────────────
def processar_pacote(lora: SX1278, trava: Trava, data: bytes, rssi: int):
    """
    Interpreta a mensagem LoRa do STM32, valida o UID e responde com ACK.

    Formato esperado: "UID:XX:XX:XX:XX\\n" (18 bytes com \\n)
    """
    try:
        msg = data.decode("ascii").strip()
    except Exception:
        log("[ LORA ] Pacote inválido (não ASCII)")
        return

    log(f"[ LORA ] RX: '{msg}'  RSSI={rssi} dBm")

    if not msg.startswith("UID:"):
        log(f"[ LORA ] Formato desconhecido: {msg}")
        return

    uid = msg[4:]  # remove "UID:" — fica "XX:XX:XX:XX"

    if uid == MASTER_UID:
        # ── ACESSO LIBERADO ──
        log("[ ACESSO ] LIBERADO ✓")
        log_gui("access", uid=uid, rssi=rssi, granted=True)

        # Envia ACK para o STM32 via LoRa
        ack = b"ACK:GRANT\n"
        lora.transmit(ack)
        log(f"[ LORA ] TX ACK: {ack.decode().strip()}")

        # Abre a trava
        trava.abrir()
        log(f"[ TRAVA ] Aberta por {UNLOCK_TIME_MS} ms")
        time.sleep_ms(UNLOCK_TIME_MS)
        trava.fechar()
        log("[ TRAVA ] Fechada")

    else:
        # ── ACESSO NEGADO ──
        log(f"[ ACESSO ] NEGADO ✗ — UID desconhecido: {uid}")
        log_gui("access", uid=uid, rssi=rssi, granted=False)

        # Envia ACK negativo para o STM32
        ack = b"ACK:DENY\n"
        lora.transmit(ack)
        log(f"[ LORA ] TX ACK: {ack.decode().strip()}")

    # Volta para RX contínuo após transmitir o ACK
    lora.start_rx()


# ─────────────────────────────────────────────────────────────────────────────
# Loop principal
# ─────────────────────────────────────────────────────────────────────────────
def main():
    log("[ SYS ] BluePASS Pico — iniciando...")

    # Inicializa LoRa
    lora = SX1278()
    try:
        lora.init()
        log("[ LORA ] SX1278 inicializado — 433 MHz SF7 BW125 CR4/5")
        print("RFID:BOOT")  # sinaliza para a GUI
    except RuntimeError as e:
        log(f"[ ERR ] {e}")
        # Pisca LED onboard para indicar erro de hardware
        led = Pin(25, Pin.OUT)
        while True:
            led.toggle()
            time.sleep_ms(200)

    # Inicializa trava
    trava = Trava(pin_num=15)
    log("[ TRAVA ] Controlador inicializado — GP15")

    # Heartbeat counter
    t_heartbeat = time.ticks_ms()

    # Inicia RX contínuo
    lora.start_rx()
    log("[ LORA ] Aguardando pacotes do STM32...")

    while True:
        # Tenta receber
        data, rssi = lora.receive()
        if data is not None:
            processar_pacote(lora, trava, data, rssi)

        # Heartbeat a cada 5s para a GUI
        if time.ticks_diff(time.ticks_ms(), t_heartbeat) >= 5000:
            t_heartbeat = time.ticks_ms()
            print("RFID:HEARTBEAT")

        time.sleep_ms(10)


# ─────────────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    main()