PWD := ${CURDIR}

thumbnail:
	docker run -v $(PWD)/input:/root/input -v $(PWD)/dcpprofiles:/root/programs/rawtherapee/dcpprofiles rawtherapee-cli_app programs/rawtherapee/rawtherapee-cli -o $(OUTPUT) -Y -d -c $(INPUT)
	# docker run -v $(PWD)/input:/root/input rawtherapee-cli_app programs/rawtherapee/rawtherapee-cli -o $(OUTPUT) -Y -d -p input/resize-fh.pp3 -c $(INPUT)

process:
	docker run -v $(PWD)/input:/root/input -v $(PWD)/dcpprofiles:/root/programs/rawtherapee/dcpprofiles rawtherapee-cli_app programs/rawtherapee/rawtherapee-cli-custom $(INPUT) $(OUTPUT) $(X) $(Y) $(L)
