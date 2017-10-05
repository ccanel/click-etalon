define($DEVNAME enp8s0d1)

define($NUMHOSTS 4)
define($IP0 10.10.1.2, $IP1 10.10.1.3, $IP2 10.10.1.4, $IP3 10.10.1.5)

define ($CIRCUIT_BW 9Gbps, $PACKET_BW 1Gbps)

define ($RECONFIG_DELAY 0.000020)
define ($TDF 1)

StaticThreadSched(hybrid_switch/packet_link0 1,
                  hybrid_switch/packet_link1 2,
                  hybrid_switch/packet_link2 3,
                  hybrid_switch/packet_link3 4,
                  hybrid_switch/circuit_link0 1,
                  hybrid_switch/circuit_link1 2,
                  hybrid_switch/circuit_link2 3,
                  hybrid_switch/circuit_link3 4,
		  runner 5,
		  sol 6,
		  )

ControlSocket("TCP", 1239)

sol :: Solstice($NUMHOSTS)
runner :: RunSchedule($NUMHOSTS)

// in :: FromDPDKDevice(0)
// out :: ToDPDKDevice(0)
in :: {InfiniteSource(LENGTH 9000) -> IPEncap(4, $IP0, $IP1) -> output}
out :: Discard

// arp_c :: Classifier(12/0800, 12/0806 20/0002)
// arp :: ARPQuerier($DEVNAME:ip, $DEVNAME:eth)

// elementclass Checker {
//     c :: IPClassifier(src host $IP0, src host $IP1, src host $IP2, src host $IP3)
//     c0, c1, c2, c3 :: Counter
//     input -> c => c0, c1, c2, c3
//           -> output
// }


// out0 :: Checker()
// out1 :: Checker()
// out2 :: Checker()
// out3 :: Checker()

hybrid_switch :: {
    c0 :: IPClassifier(dst host $IP0, dst host $IP1, dst host $IP2, dst host $IP3)
    c1 :: IPClassifier(dst host $IP0, dst host $IP1, dst host $IP2, dst host $IP3)
    c2 :: IPClassifier(dst host $IP0, dst host $IP1, dst host $IP2, dst host $IP3)
    c3 :: IPClassifier(dst host $IP0, dst host $IP1, dst host $IP2, dst host $IP3)

    q00, q01, q02, q03 :: Queue(CAPACITY 1000)
    q10, q11, q12, q13 :: Queue(CAPACITY 1000)
    q20, q21, q22, q23 :: Queue(CAPACITY 1000)
    q30, q31, q32, q33 :: Queue(CAPACITY 1000)

    packet_link0, packet_link1, packet_link2, packet_link3 :: {
      input[0,1,2,3] => RoundRobinSched -> LinkUnqueue(0, $PACKET_BW)
                     // -> StoreData(4, DATA \<01>)
		     -> output
    }

    circuit_link0, circuit_link1, circuit_link2, circuit_link3 :: {
      input[0,1,2,3] => ps :: PullSwitch -> LinkUnqueue(0, $CIRCUIT_BW)
                     // -> StoreData(4, DATA \<02>)
		     -> output
    }

    // ecnr0, ecnr1, ecnr2, ecnr3 :: {
    //     s :: Switch(4)
    // 	w0 :: StoreData(1, DATA \<00>)
    // 	w1 :: StoreData(1, DATA \<01>)
    // 	w2 :: StoreData(1, DATA \<02>)
    // 	w3 :: StoreData(1, DATA \<03>)
    // 	wNone :: StoreData(1, DATA \<FC>)
    // 	input -> s => w0, w1, w2, w3, wNone -> output
    // }

    input[0] -> c0 => q00, q01, q02, q03
    input[1] -> c1 => q10, q11, q12, q13
    input[2] -> c2 => q20, q21, q22, q23
    input[3] -> c3 => q30, q31, q32, q33

    q00, q10, q20, q30 => packet_link0 -> [0]output //ecnr0
    q01, q11, q21, q31 => packet_link1 -> [1]output //ecnr1
    q02, q12, q22, q32 => packet_link2 -> [2]output //ecnr2
    q03, q13, q23, q33 => packet_link3 -> [3]output //ecnr3

    q00, q10, q20, q30 => circuit_link0 -> [0]output //ecnr0
    q01, q11, q21, q31 => circuit_link1 -> [1]output //ecnr1
    q02, q12, q22, q32 => circuit_link2 -> [2]output //ecnr2
    q03, q13, q23, q33 => circuit_link3 -> [3]output //ecnr3

    // ecnr0, ecnr1, ecnr2, ecnr3 => [0,1,2,3]output
}

// in -> arp_c -> MarkIPHeader(14) -> StripToNetworkHeader -> GetIPAddress(16)
in
   -> IPClassifier(src host $IP0, src host $IP1,
                   src host $IP2, src host $IP3)[0,1,2,3]
   => hybrid_switch[0,1,2,3]
   // => out0, out1, out2, out3 -> SetIPChecksum
   // -> arp -> out
   -> out

// arp_c[1] -> [1]arp
