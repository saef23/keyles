} else {
      sedangMenekanWakeup = false;
    }

    // 2. Wake up dari getar (2 bip pendek)
    if (!siapBangunGetar) {
      if (digitalRead(PIN_SIMULASI_GETAR) == LOW) siapBangunGetar = true;
    } else {
      if (digitalRead(PIN_SIMULASI_GETAR) == HIGH) {
        if (!sedangMendeteksiGetar) {
          waktuGetarMulai = millis();
          sedangMendeteksiGetar = true;
        } else if (millis() - waktuGetarMulai >= durasiStabilGetarMs) {
          bunyiWakeupGetar();
          keluarDariStandby();
          return;
        }
      } else {
        sedangMendeteksiGetar = false;
      }
    }

    delay(20);
    return;
  }

  if (cekRFID()) delay(1500);

  if (sedangDiModeWakeup && !statusTerdeteksiSebelumnya && !rfidKontakState) {
    if (millis() - waktuMasukModeWakeup >= TIMEOUT_WAKEUP_KE_STANDBY) {
      masukModeStandby();
      return;
    }
  }

  if (sedangMenungguStandbyRFID) {
    if (millis() - waktuMulaiStandbyRFID >= 10000) {
      masukModeStandby();
      return;
    }
    delay(50);
    return;
  }

  if (keynumber > 0 || rfidAktif) detectwifi();
}
