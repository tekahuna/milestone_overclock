SUBDIRS = cm_cosmo lge_cosmo_froyo lge_cosmo_gb moto_droid3 moto_bionic moto_razr kindle_fire nook_tablet samsung_i9100g
.PHONY: $(SUBDIRS)

all:	$(SUBDIRS)

cm_cosmo:
	cd cm_cosmo && make

lge_cosmo_froyo:
	cd lge_cosmo_froyo && make

lge_cosmo_gb:
	cd lge_cosmo_gb && make

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

clean_lge_cosmo_froyo:
	cd lge_cosmo_froyo && make clean

clean_lge_cosmo_gb:
	cd lge_cosmo_gb && make clean

clean_moto_droid3:
	cd moto_droid3 && make clean

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

clean:	clean_cm_cosmo clean_lge_cosmo_froyo clean_lge_cosmo_gb clean_moto_droid3 clean_moto_bionic clean_moto_razr clean_kindle_fire clean_nook_tablet clean_samsung_i9100g
	rm -f *~

dist:
	tar cvfz ../opperator.tar.gz ../opperator/
