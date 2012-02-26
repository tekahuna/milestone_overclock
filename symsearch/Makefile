SUBDIRS = cm_cosmo lge_p970 lge_cosmo_froyo lge_cosmo_gb moto_atrix2 moto_xboard moto_droid3 moto_bionic moto_razr kindle_fire nook_tablet samsung_i9100g
.PHONY: $(SUBDIRS)

all:	$(SUBDIRS)

cm_cosmo:
	cd cm_cosmo && make

lge_p970:
	cd lge_p970 && make

lge_cosmo_froyo:
	cd lge_cosmo_froyo && make

lge_cosmo_gb:
	cd lge_cosmo_gb && make

moto_atrix2:
	cd moto_atrix2 && make

moto_xyboard:
	cd moto_xyboard && make

moto_droid3:
	cd moto_droid3 && make

moto_bionic:
	cd moto_bionic && make

moto_razr:
	cd moto_razr && make

kindle_fire:
	cd kindle_fire && make

nook_tablet:
	cd nook_tablet && make

samsung_i9100g:
	cd samsung_i9100g && make

clean_cm_cosmo:
	cd cm_cosmo && make clean

clean_lge_p970:
	cd lge_p970 && make clean

clean_lge_cosmo_froyo:
	cd lge_cosmo_froyo && make clean

clean_lge_cosmo_gb:
	cd lge_cosmo_gb && make clean

clean_moto_droid3:
	cd moto_droid3 && make clean

clean_moto_atrix2:
	cd moto_atrix2 && make clean

clean_moto_xyboard:
	cd moto_xyboard && make clean

clean_moto_bionic:
	cd moto_bionic && make clean

clean_moto_razr:
	cd moto_razr && make clean

clean_kindle_fire:
	cd kindle_fire && make clean

clean_nook_tablet:
	cd nook_tablet && make clean

clean_samsung_i9100g:
	cd samsung_i9100g && make clean

clean:	clean_cm_cosmo clean_lge_p970 clean_lge_cosmo_froyo clean_lge_cosmo_gb clean_moto_droid3 clean_moto_atrix2 clean_moto_xyboard clean_moto_bionic clean_moto_razr clean_kindle_fire clean_nook_tablet clean_samsung_i9100g
	rm -f *~

dist:
	tar cvfz ../symsearch.tar.gz ../symsearch/
