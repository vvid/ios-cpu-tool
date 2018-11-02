//
//  measureCpufreq.swift
//  ios-cpu-tool
//
//  Created by vvid on 29/11/2018.
//  Copyright © 2018 vvid. All rights reserved.
//

import Foundation

class CoreInfo
{
    var minFreqMHz: Int = 0
    var maxFreqMHz: Int = 0
    var signature: Int = 0
}

class CpuInfo
{
    let maxThreads: Int = 8
    var numCores  : Int = 1
    var actCores  : Int = 1

    func setup()
    {
        numCores = ProcessInfo.processInfo.processorCount
        actCores = ProcessInfo.processInfo.activeProcessorCount
    }
    func limitNumThreads(_ val: Int ) -> Int
    {
        return min(max(val, 1), numCores)
    }
}

class MeasureCpuFreq {

    var runThreads : Int = 0

    func measureFreq(_ cpuInfo: CpuInfo, _ numThreads: Int) {
      runThreads = numThreads
      calculate_freq_start(Int32(runThreads), 1000)
    }

    func measureBoost(_ cpuInfo: CpuInfo, _ numThreads: Int) {
        runThreads = numThreads
        measure_boost_start(Int32(runThreads), 0,0,0)
    }
}
