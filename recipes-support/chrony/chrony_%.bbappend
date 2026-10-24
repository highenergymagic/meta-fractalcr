# The appliance supervisor owns startup, writable state and configuration.
PACKAGECONFIG = "privdrop scfilter ipv6"
# shadow requires an explicit primary group when the recipe creates it first.
USERADD_PARAM:${PN} = "--system --gid chronyd -d / -M --shell /sbin/nologin chronyd;"
