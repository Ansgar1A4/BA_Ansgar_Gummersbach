# Install SPANK-Plugin

bin_dir=/bin
lib_dir=/usr/lib64/

cd /tmp
wget https://raw.githubusercontent.com/Ansgar1A4/BA_Ansgar_Gummersbach/Abgabe/plugins/lo2s-plugin/src/lo2do.c

gcc -shared -fPIC -I/usr/include/slurm -o $lib_dir/lo2do.so lo2do.c
cd /tmp
rm -f lo2do.c

# Add Plugin to plugstack.conf:
touch /etc/slurm/plugstack.conf

#TODO: Check if line already exists in plugstack.conf, if not add it
echo "required   $lib_dir/lo2do.so" > /etc/slurm/plugstack.conf
