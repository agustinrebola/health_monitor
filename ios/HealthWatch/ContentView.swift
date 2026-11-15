import SwiftUI
import Charts

private struct HeartPoint: Identifiable {
    let id: Date
    let bpm: Int
}

struct ContentView: View {
    @EnvironmentObject private var bluetooth: BluetoothManager
    @EnvironmentObject private var history: HistoryStore
    @State private var exportURL: URL?
    var body: some View {
        TabView {
            dashboard.tabItem { Label("Dashboard", systemImage: "heart.text.square") }
            activity.tabItem { Label("Activity", systemImage: "figure.walk") }
            physiology.tabItem { Label("Heart", systemImage: "heart.fill") }
            device.tabItem { Label("Device", systemImage: "applewatch") }
        }
    }
    private var dashboard: some View {
        NavigationStack {
            List {
                if bluetooth.snapshot?.possibleFall == true {
                    Text("Possible fall detected (experimental). Check the wearer.")
                        .foregroundStyle(.red)
                }
                LabeledContent("Connection", value: bluetooth.status)
                LabeledContent("Heart rate", value: bluetooth.snapshot?.heartRate.map { "\($0) bpm" } ?? "Unavailable")
                LabeledContent("SpO₂", value: "Unavailable — experimental")
                LabeledContent("Steps today", value: "\(history.dailySteps(live: bluetooth.snapshot))")
                LabeledContent("Activity", value: bluetooth.snapshot?.activity.title ?? "Unknown")
                LabeledContent("Cadence", value: bluetooth.snapshot.map { "\($0.cadence) steps/min" } ?? "—")
                LabeledContent("Optical contact", value: bluetooth.snapshot?.contactGood == true ? "Detected" : "Unconfirmed")
                LabeledContent("PPG quality", value: bluetooth.snapshot?.ppgQuality.title ?? "Unknown")
                LabeledContent("Last update", value: bluetooth.snapshot?.receivedAt.formatted(date: .abbreviated, time: .standard) ?? "Never")
            }.navigationTitle("HealthWatch")
        }
    }
    private var activity: some View {
        NavigationStack {
            List {
                LabeledContent("Steps today", value: "\(history.dailySteps(live: bluetooth.snapshot))")
                Section("Recent activity") {
                    ForEach(history.records.suffix(100).reversed()) { record in
                        HStack {
                            Text(record.receivedAt, style: .time)
                            Spacer()
                            Text(record.activity.title)
                            Text("\(record.steps)").monospacedDigit()
                        }
                    }
                }
            }.navigationTitle("Activity")
        }
    }
    private var physiology: some View {
        NavigationStack {
            List {
                LabeledContent("Current heart rate", value: bluetooth.snapshot?.heartRate.map { "\($0) bpm" } ?? "Unavailable")
                Section("Recent valid heart rate") {
                    Chart(history.records.suffix(300).compactMap { r -> HeartPoint? in
                        guard let hr = r.heartRate else { return nil }
                        return HeartPoint(id: r.receivedAt, bpm: Int(hr))
                    }) { point in
                        LineMark(x: .value("Time", point.id), y: .value("BPM", point.bpm))
                    }.frame(height: 180)
                }
                Text("SpO₂ is unavailable until the optical assembly is calibrated and validated.")
                    .font(.footnote)
            }.navigationTitle("Physiology")
        }
    }
    private var device: some View {
        NavigationStack {
            List {
                LabeledContent("Status", value: bluetooth.status)
                Button("Scan for HealthWatch") { bluetooth.scan() }
                ForEach(bluetooth.devices, id: \.identifier) { device in
                    Button(device.name ?? device.identifier.uuidString) { bluetooth.connect(device) }
                }
                if bluetooth.connected { Button("Disconnect") { bluetooth.disconnect() } }
                Section("Diagnostics") {
                    LabeledContent("IMU", value: bluetooth.snapshot?.imuReady == true ? "Ready" : "Unavailable")
                    LabeledContent("MAX30102", value: bluetooth.snapshot?.ppgReady == true ? "Ready" : "Unavailable")
                    LabeledContent("Invalid BLE packets", value: "\(bluetooth.invalidPackets)")
                    LabeledContent("Protocol", value: "1")
                }
                Toggle("Detailed recording", isOn: $history.detailedRecording)
                Text("Stores every received snapshot for a short test. Turn off afterward to restore minute history.")
                    .font(.footnote)
                Button("Prepare CSV export") { exportURL = history.exportCSV() }
                if let exportURL { ShareLink("Share CSV", item: exportURL) }
            }.navigationTitle("Device")
        }
    }
}
