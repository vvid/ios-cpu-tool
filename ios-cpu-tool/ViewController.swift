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

    func textFieldDidEndEditing(_ sender: UITextField) {
        if let val = Int(sender.text!)
        {
          numThreads = min(max(val, 1), maxThreads)
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

    @IBAction func startFreqCalc(_ sender: UIButton) {

        warmup();

        var threadTimes = [Double](repeating: 0.0, count: maxThreads)

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


        var str: String = ""
        for i in 1...numThreads
        {
            str = str + "Core " + String(i) + ": " + String(Int(threadTimes[i-1] / 1000000)) + "Mhz\n"
        }

        textOutput.text = str
    }
    override func viewDidLoad() {
        super.viewDidLoad()
        textFieldThreads.delegate = self
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }


}

