# R44 SD originali RC2

[Scarica l’installatore completo](https://github.com/citroale90/qc5-t23-firmware/releases/download/r44-sd-originali-rc2/QC5-R44-SD-ORIGINALI-RC2.zip).

Corregge l’arresto dell’installatore RC1 dopo i due backup su una gemella
con hardware compatibile ma una diversa versione del software originale.

- Elimina il vincolo sulle impronte di versione di ROOT/SYSTEM.
- Accetta padding inutilizzato e ENV di fabbrica uniformemente vuoti a zero
  oppure FF; resta obbligatorio il CRC per ENV popolati.
- Registra codice di uscita del preparatore e motivo preciso dei rifiuti.
- Conserva due letture indipendenti, backup e ripristino della singola unità,
  controlli su geometria/layout, MAC originale e trasporto U-Boot/kernel.

L’immagine nativa R44, i driver e il caricatore di ripristino restano identici
a RC1. L’hardware supportato è QC5_YA_CODE_V82, T23DL, GC1084, AIC8800DC USB,
NOR 8 MiB con il layout e trasporto di recupero supportati. Il solo T23
non dimostra compatibilità. Il pacchetto non identifica automaticamente
qualsiasi periferica di una scheda sconosciuta.

## Installazione, backup e ripristino

Estrarre lo ZIP e seguire `AVVIO-RAPIDO.txt`. Da computer eseguire:

```sh
python3 prepara_sd.py --sd "/percorso/della/SD"
```

Inserire la SD FAT32/MBR nella camera originale spenta. Alimentare tenendo
Reset, rilasciandolo dopo circa tre secondi, poi lasciare alimentata almeno
quindici minuti senza QR o altri Reset. Le credenziali possono essere
inserite in `config.txt`, oppure tramite l’hotspot locale dopo l’installazione.
Ogni unità crea i propri backup in `qc5-backups/unit-*`; conservarli.

Per predisporre il ripristino originale della stessa unità:

```sh
python3 ripristina_sd.py --sd "/percorso/della/SD" --elenca
python3 ripristina_sd.py --sd "/percorso/della/SD" --prepara-ripristino
```

Inserire la SD e accendere senza Reset, attendere quindici minuti, quindi
rimuovere/disattivare la SD prima di un’altra accensione. U-Boot deve essere
integro. Capsule e identità di una camera non vanno clonate sulle altre.

## Verifiche e limiti

Passano 30 casi C con dispositivi simulati, compreso il backup originale
indipendente della variante che falliva, quattro percorsi reali degli script
e 13 prove degli strumenti PC sul kit RC2. Il preparatore e il modulo ENV
sono ricompilati dai sorgenti pubblici e confrontati byte per byte.
I controlli precedenti del caricatore e del modulo ENV restano applicabili
perché quei binari non cambiano. La prima installazione completa a freddo
della RC2 sulla gemella resta da collaudare fisicamente: è una prerelease.

Se torna l’applicazione originale, leggere `qc5-installer/avvio.log` e
`qc5-backups/unit-*/prepare.log` prima di ripetere Reset. Il LED bianco
non dimostra che la scrittura sia iniziata.

[Sorgenti e dettagli](../../native/installer/common/README.md).
[Licenze e limiti dei componenti](../../NOTICE.md). Lo ZIP non contiene
backup, credenziali o chiavi di altre unità; le attribuzioni e i limiti
di redistribuzione già documentati restano invariati.
