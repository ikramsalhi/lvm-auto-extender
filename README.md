**Étudiant** : Ikram Salhi  
**Module** : Systèmes Distribués / Administration Système  
**Date** : Décembre 2025  

---

## 🎯 Objectif

Développer un système autonome de gestion de volumes logiques (LVM) capable d’**étendre automatiquement** un volume critique (`lv_home`) lorsqu’il atteint un seuil d’utilisation (80%), en suivant une stratégie en 3 étapes :

1. Utiliser l’espace libre disponible dans le groupe de volumes (VG)  
2. Réduire dynamiquement des volumes frères sous-utilisés (*donors*)  
3. Ajouter un disque de secours (`/dev/sdc`) au VG si nécessaire  

---

## 🛠️ Environnement

- **OS** : Ubuntu 24.04 LTS (machine virtuelle VirtualBox)  
- **Disques** :
  - `/dev/sda` : système (25 Go)
  - `/dev/sdb` : 10 Go — PV principal → VG `vgdata`
  - `/dev/sdc` : 10 Go — PV de secours (activé à la demande)

---

## 🧱 Architecture LVM

| Élément | Taille | Rôle |
|--------|--------|------|
| `vgdata` | 10 Go | Volume Group |
| `lv_home` | 5 Go | Volume cible (auto-étendu) |
| `lv_data1` | 3 Go | Donor potentiel |
| `lv_data2` | 1 Go | Donor secondaire (test réduction) |

```bash
$ sudo lvs
  LV       VG     Attr       LSize Pool Origin Data%  Meta%  Move Log Cpy%Sync Convert
  lv_home  vgdata -wi-ao---- 5.00g
  lv_data1 vgdata -wi-ao---- 3.00g
  lv_data2 vgdata -wi-ao---- 1.00g