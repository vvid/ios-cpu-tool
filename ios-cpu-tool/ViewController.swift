//
//  ViewController.swift
//  ios-cpu-tool
//
//  Created by vvid on 12/10/2017.
//  Copyright © 2017 vvid. All rights reserved.
//

import UIKit

class ViewController: UIViewController, UITextFieldDelegate {

    @IBOutlet weak var textFieldThreads: UITextField!
    @IBOutlet weak var textOutput: UITextView!
    @IBOutlet weak var buttonStartStop: UIButton!
    @IBOutlet weak var buttonBoost: UIButton!
    @IBOutlet weak var buttonMemLat: UIButton!
    @IBOutlet weak var buttonBandwidth: UIButton!

    var started: Bool = false
    var numThreads: Int = 1
    var cpuInfo = CpuInfo();
    var measureFreq = MeasureCpuFreq();

    func textFieldDidEndEditing(_ sender: UITextField) {
        if let val = Int(sender.text!)
        {
            numThreads = cpuInfo.limitNumThreads(val)
            sender.text = String(numThreads)
        }
    }

    func textFieldShouldReturn(_ sender: UITextField) -> Bool {
        if let _ = Int(sender.text!)
        {
            sender.resignFirstResponder()
            return true
        }
        else
        {
            return false
        }
    }

    func freqInMHz(_ freq: Double) -> String {
      return String(Int((freq + 499999) / 1000000))
    }

    @IBAction func startFreqCalc(_ sender: UIButton) {

 //       warmup();

        //var threadTimes = [Double](repeating: 0.0, count: maxThreads)
        /*
        let group = DispatchGroup()
        let queue = DispatchQueue.global(qos: .userInteractive)
        for i in 1...numThreads
        {
            queue.async(group: group)
            {
                threadTimes[i-1] = measure_workload();
            }
        }
        _ = group.wait(timeout: DispatchTime.distantFuture)

 */
        if (started)
        {
          started = false
          calculate_freq_stop()
          sender.setTitle("Max freq", for: .normal)

            var str: String = ""
            for i in 1...measureFreq.runThreads
            {
                str = str + "Core " + String(i) + ": " + freqInMHz(get_thread_freq(Int32(i-1))) + "MHz (id="
                                                       + freqInMHz(get_thread_min_freq(Int32(i-1))) + " +"
                                                       + freqInMHz(get_thread_max_freq(Int32(i-1))) + "ms)\n"
            }

            textOutput.text = str
        }
        else
        {
          sender.setTitle("(Stop)", for: .normal)
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

            textOutput.text = str
        }
        else
        {
            sender.setTitle("(Stop)", for: .normal)
            measureFreq.measureBoost(cpuInfo, numThreads)
            started = true
        }
    }

    @IBAction func buttonMemLatAction(_ sender: UIButton) {
    }

    @IBAction func buttonMemBandwidthAction(_ sender: UIButton) {
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        textFieldThreads.delegate = self
        cpuInfo.setup()
        textOutput.text = "Cores: " + String(cpuInfo.numCores) + "\nActive Cores: " + String(cpuInfo.actCores)
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
}

