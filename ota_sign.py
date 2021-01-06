import os

Import("env", "projenv")
print("THIS IS POST SCRIPT")


def before_build():
	signing_script = "~/.platformio/packages/framework-arduinoespressif8266/tools/signing.py"
	file_to_sign = "src/OTAPublicKey.h"
	public_key = "~/projects/firmware_keys/public_intexspa.key"
	print ("Signing: " + file_to_sign)
	print ("\n")
	cmd = "python3 "
	cmd += signing_script + " --mode header"
	cmd += " --publickey " + public_key
	cmd += " --out " + file_to_sign
	print ("Running CMD: " + cmd)
	print ("\n")
	os.system(cmd)

before_build()



def after_build(source, target, env):
	signing_script = "~/.platformio/packages/framework-arduinoespressif8266/tools/signing.py"
	file_to_sign = target[0].get_path()
	output_file = target[0].get_path()  + ".signed"
	private_key = "~/projects/firmware_keys/private_intexspa.key"
	print ("Signing: " + file_to_sign)
	print ("\n")
	cmd = "python3 "
	cmd += signing_script + " --mode sign"
	cmd += " --privatekey " + private_key
	cmd += " --bin " + file_to_sign
	cmd += " --out " + output_file
	print ("Running CMD: " + cmd)
	print ("\n")
	#os.system("rm " + output_file)
	os.system(cmd)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)

