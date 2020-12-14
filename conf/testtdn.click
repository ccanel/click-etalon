elementclass TDNTester {
  $device, $src, $base_ip, $ntdn, $nrack, $nhost |

  source :: ICMPTDNUpdate($src, BASE $base_ip, NTDN $ntdn, NRACK $nrack, NHOST $nhost, TEST true);
  source -> EtherEncap(0x0800, 00:00:c0:ae:67:ef, 00:00:c0:4f:71:ef) -> Queue(64) -> td :: ToDevice($device);
}

// create a TDNTester

t :: TDNTester(ens39, 10.0.1.1, 10.1.0.0, 2, 3, 2);

