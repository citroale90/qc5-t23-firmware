> Aggiornamento: usare [RC2](r44-sd-originali-rc2.md). RC1 può fermarsi prima della scrittura su gemelle con software originale diverso e padding a zero.

# QC5 R44 RC1 — installatore SD per telecamere gemelle originali

**Prerelease per il primo collaudo su una gemella ancora originale.**
La R44 e il verificatore U-Boot sono già qualificati sulla camera di
riferimento. La prima installazione a freddo di questa confezione su una
gemella non è ancora stata verificata fisicamente.

[Scarica QC5-R44-SD-ORIGINALI.zip](https://github.com/citroale90/qc5-t23-firmware/releases/download/r44-sd-originali-rc1/QC5-R44-SD-ORIGINALI.zip)

SHA-256:
`144cef5888351d327b987cfff701d45d7de33bb3371dc5a9a7f5606dcdfe657e`

## Compatibilità

QC5_YA_CODE_V82, Ingenic T23DL, sensore GC1084, AIC8800DC USB e NOR da 8 MiB,
con il profilo originale verificato del kernel `3.10.14__isvp_pike_1.0__`.
Il preparatore controlla bootloader, kernel, ROOT/SYSTEM, geometria, MAC
originale in PARAM e aree riservate. Varianti non riconosciute vengono
rifiutate prima dell'installazione; il solo processore T23 non basta.

## Installazione senza IA, NAS o app

1. Scarica ed estrai lo ZIP sul computer. Prepara una SD già **FAT32/MBR**,
   una sola partizione, cluster da 8–64 KiB e almeno 160 MiB liberi.
2. Dalla cartella estratta esegui con Python 3.9 o successivo:
   ```sh
   python3 prepara_sd.py --sd "/percorso/della/SD"
   ```
   Sul Mac il percorso è, per esempio, `/Volumes/SENZA NOME`. Il programma
   non formatta e conserva configurazione, registrazioni e backup esistenti.
3. Facoltativamente inserisci SSID e PWD nel `config.txt` della radice.
   Lasciandoli vuoti, configura il Wi-Fi dall'hotspot locale dopo l'installazione.
4. Espelli normalmente la SD e inseriscila nella gemella originale spenta.
   Alimenta tenendo premuto Reset e rilascialo dopo circa **3 secondi**.
5. Lascia alimentata la camera per **almeno 15 minuti**, senza QR, altri
   Reset o interruzioni. Il programma crea e verifica i backup, prepara il
   recupero SD se necessario e riavvia verso l'installatore U-Boot.
6. Dopo l'avvio usa l'indirizzo assegnato dal router: WebUI `http://IP_CAMERA/`,
   RTSP `rtsp://IP_CAMERA:8554/live` e SSH `root@IP_CAMERA`, senza password
   come previsto per questa configurazione locale.

Senza credenziali salvate la camera crea **QC5-xxxxxx**. Collegati anche se
indica “senza Internet”; la pagina dovrebbe aprirsi automaticamente.
L'indirizzo manuale è `http://192.168.168.1/setup`.
Wi-Fi e chiave SSH personale vengono poi salvati nella flash; la SD è opzionale
per sistema, rete e SSH. Parla dal browser richiede il proprio proxy HTTPS.
`NTP_SERVER="auto"` usa il server locale del router ed è modificabile.

## Backup automatico per ogni unità

Prima delle modifiche vengono salvate **due letture complete da 8 MiB**,
confrontate e rilette. Ogni cartella `qc5-backups/unit-XXXXXX/` conserva:

- `original-1.bin` e `original-2.bin`;
- aggiornamento e ripristino legati alle impronte della singola camera;
- caricatore, manifesto e istruzioni di recupero abbinati.

Non vengono clonati MAC, seriale, credenziali o chiavi SSH della camera di
riferimento. Conserva anche fuori dalla SD la cartella `qc5-backups` e mantienila
privata. Per un'altra gemella esegui nuovamente `prepara_sd.py`: i backup
precedenti vengono conservati.

## Ripristino da SD

A camera spenta e con la SD nel computer:

```sh
python3 ripristina_sd.py --sd "/percorso/SD" --elenca
python3 ripristina_sd.py --sd "/percorso/SD" --prepara-ripristino
```

Per scegliere una specifica camera aggiungi
`--backup qc5-backups/unit-XXXXXX` usando la sua cartella.
Espelli la SD, inseriscila nella camera corretta e alimenta **senza Reset**.
Attendi almeno 15 minuti. Dopo il ripristino togli la SD prima della prossima
accensione oppure esegui dal computer:

```sh
python3 ripristina_sd.py --sd "/percorso/SD" --disattiva
```

Il recupero usa U-Boot e non richiede un Linux funzionante. Ripristina
KERNEL/ROOT/SYSTEM, conserva BOOT/WWW/PARAM e le impostazioni di recupero;
il ritorno all'originale cancella le aree native Wi-Fi/SSH.
La preparazione rimuove il segnale dell'installatore, evitando una nuova
installazione subito dopo il ripristino.
**Serve U-Boot integro:** questa procedura non ripara un bootloader danneggiato.

## Stato e verifiche

```sh
python3 ripristina_sd.py --sd "/percorso/SD" --stato
```

Fase 8 con esito positivo conclude la transazione; un errore non equivale a
completamento. Non dedurre la riuscita dai soli LED o dai messaggi vocali.
Sono passati 25 casi C, 20 prove MIPS del modulo ENV, quattro percorsi degli
script, nove casi di ripristino, 13 prove degli strumenti PC e sette casi
con U-Boot originale in emulazione. Il builder pubblico riproduce esattamente
i due programmi inclusi. I dettagli sono in `QUALIFICA.json`.

L'installatore può configurare il solo settore ENV da 4 KiB quando necessario,
con CRC finale e rilettura. Non riscrive il codice U-Boot, WWW o PARAM.
L'archivio non contiene backup o chiavi della camera di riferimento.

## Sorgenti e componenti

[Sorgenti, licenze per file e istruzioni](https://github.com/citroale90/qc5-t23-firmware/tree/r44-sd-originali-rc1/native/installer/common).
I driver Linux hanno sorgenti identificati. Il firmware interno Wi-Fi resta
binario e invariato; questa release **non dichiara l'intero firmware open source**.
Gli avvisi e i punti di provenienza/licenza ancora aperti sono descritti in
`SORGENTI-E-LICENZE.txt` e nel repository. Testi di licenza e avvisi sono
forniti come allegato separato; non costituiscono una nuova licenza globale.
