"""QBR inter-host scheduling on CloudLab Utah physical nodes.

Instructions:
The default is three c6525-25g nodes running Ubuntu 22.04:
combined client/dispatcher 10.10.1.1, workers 10.10.1.11 onward.
Pin load generation and dispatch to separate physical cores on the client.
An optional separate dispatcher uses 10.10.1.2 on an additional machine.
Use the 25 Gbps experiment LAN for all benchmark traffic. Start with eight
execution cores and one separate controller core per worker. Collect logs
after runs. Resources only: no scheduler software is installed or started.
"""
import geni.portal as portal
import geni.rspec.pg as pg

pc = portal.Context()
pc.defineParameter("workers", "Execution hosts (plus the client and optional dispatcher)", portal.ParameterType.INTEGER, 2)
pc.defineParameter("separate_dispatcher", "Allocate a separate dispatcher machine", portal.ParameterType.BOOLEAN, False)
pc.defineParameter("hardware", "Physical hardware type (same for every node)", portal.ParameterType.STRING, "c6525-25g")
pc.defineParameter("image", "Linux disk image URN", portal.ParameterType.STRING,
                   "urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU22-64-STD")
params = pc.bindParameters()
max_workers = 14 if params.separate_dispatcher else 15
if params.workers < 2 or params.workers > max_workers:
    pc.reportError(portal.ParameterError("Use at least 2 execution hosts and at most 16 total nodes", ["workers", "separate_dispatcher"]))
if not params.hardware.strip() or not params.image.strip():
    pc.reportError(portal.ParameterError("Specify a hardware type and Linux image", ["hardware", "image"]))
pc.verifyParameters()
request = pc.makeRequestRSpec()
site = "urn:publicid:IDN+utah.cloudlab.us+authority+cm"
lan = request.LAN("exp-lan")
lan.bandwidth = 25000000  # Kbps, per LAN attachment.
lan.addComponentManager(site)
roles = [("client", 1)]
if params.separate_dispatcher:
    roles.append(("dispatcher", 2))
roles.extend(("worker" + str(i), 10 + i) for i in range(1, params.workers + 1))
for role, suffix in roles:
    node = request.RawPC(role)
    node.component_manager_id = site
    node.hardware_type = params.hardware
    node.disk_image = params.image
    iface = node.addInterface("if0")
    iface.addAddress(pg.IPv4Address("10.10.1." + str(suffix), "255.255.255.0"))
    lan.addInterface(iface)
pc.printRequestRSpec(request)
