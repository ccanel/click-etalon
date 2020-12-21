elementclass TDNTester {
  $device, $src_ip, $src_eth, $base_ip, $base_eth, $ctrlnic, $ntdn, $nrack, $nhost |

  source :: ICMPTDNUpdate($src_ip, $src_eth, $base_ip, $base_eth, $ctrlnic, NTDN $ntdn, NRACK $nrack, NHOST $nhost, TEST true);
  source -> Queue(64) -> td :: ToDevice($device);
}

// create a TDNTester

t :: TDNTester(ens39, 10.0.1.1, 11:22:33:44:55:66, 10.1.0.0, aa:aa:aa:00:00:00, 3, 2, 3, 2);

