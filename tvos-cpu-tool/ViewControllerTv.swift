//
//  ViewController.swift
//  tvos-cpu-tool
//
//  Created by vvid on 02/11/2018.
//  Copyright © 2018 vvid. All rights reserved.
//

import UIKit

class ViewController: UIViewController {

    @IBOutlet weak var segControlNumCores: UISegmentedControl!
    @IBOutlet weak var buttonGetFreq: UIButton!
    @IBOutlet weak var buttonBoost: UIButton!

    var started: Bool = false
    var numThreads: Int = 1
    var cpuInfo = CpuInfo();
    var measureFreq = MeasureCpuFreq();

    @IBAction func numCoresChanged(_ sender: UISegmentedControl) {
        let value = sender.selectedSegmentIndex + 1
        numThreads = cpuInfo.limitNumThreads(value)
    }

    func freqInMHz(_ freq: Double) -> String {
        return String(Int((freq + 499999) / 1000000))
    }

    @IBAction func startFreqCalc(_ sender: UIButton) {
        if (started)
        {
            started = false
            calculate_freq_stop()
            sender.setTitle("Get CPU freq", for: .normal)

            var str: String = ""
            for i in 1...measureFreq.runThreads
            {
                str = str + "Core " + String(i) + ": " + freqInMHz(get_thread_freq(Int32(i-1))) + "MHz (id="
                    + freqInMHz(get_thread_min_freq(Int32(i-1))) + " +"
                    + freqInMHz(get_thread_max_freq(Int32(i-1))) + "ms)\n"
            }

       //     textOutput.text = str
        //    testArray();
        }
        else
        {
            sender.setTitle("Stop", for: .normal)
            measureFreq.measureFreq(cpuInfo, numThreads);
            started = true
        }
    }

    @IBAction func buttonBoostAction(_ sender: UIButton) {
        if (started)
        {
            started = false
            measure_boost_stop()
            sender.setTitle("Boost", for: .normal)

            var str: String = ""
            for i in 1...measureFreq.runThreads
            {
                str = str + "Core " + String(i) + ": " + freqInMHz(get_thread_freq(Int32(i-1))) + "MHz (id="
                    + freqInMHz(get_thread_min_freq(Int32(i-1))) + " +"
                    + freqInMHz(get_thread_max_freq(Int32(i-1))) + "ms)\n"
            }

      //      textOutput.text = str
        }
        else
        {
            sender.setTitle("(Stop)", for: .normal)
            measureFreq.measureBoost(cpuInfo, numThreads)
            started = true
        }
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        cpuInfo.setup()

        for i in 0..<cpuInfo.numCores {
            segControlNumCores.setEnabled(true, forSegmentAt: i)
        }
    }


}

