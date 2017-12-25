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

    var started: Bool = false
    var numThreads: Int = 1
    let maxThreads: Int = 6
    var numCpuCores : Int = 1
    var actCpuCores : Int = 1
    var runThreads : Int = 0

    func textFieldDidEndEditing(_ sender: UITextField) {
        if let val = Int(sender.text!)
        {
          numThreads = min(max(val, 1), numCpuCores)
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
          calibrate_stop_threads()
          sender.setTitle("Get CPU freq", for: .normal)

            var str: String = ""
            for i in 1...runThreads
            {
                str = str + "Core " + String(i) + ": " + freqInMHz(get_thread_freq(Int32(i-1))) + "MHz (id="
                                                       + freqInMHz(get_thread_min_freq(Int32(i-1))) + " +"
                                                       + freqInMHz(get_thread_max_freq(Int32(i-1))) + "ms)\n"
            }

            textOutput.text = str
        }
        else
        {
          sender.setTitle("Stop", for: .normal)
          runThreads = numThreads
          calibrate_start_threads(Int32(numThreads), 1000)
          started = true
        }
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        textFieldThreads.delegate = self
        numCpuCores = ProcessInfo.processInfo.processorCount
        actCpuCores = ProcessInfo.processInfo.activeProcessorCount
        textOutput.text = "Cores: " + String(numCpuCores) + "\nActive Cores: " + String(actCpuCores)
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }


}

