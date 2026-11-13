import Foundation
import Combine

@MainActor
final class HistoryStore: ObservableObject {
    @Published private(set) var records: [WatchSnapshot] = []
    @Published var detailedRecording = false
    private let fileURL: URL

    init() {
        let directory = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        fileURL = directory.appendingPathComponent("snapshots.json")
        if let data = try? Data(contentsOf: fileURL), let saved = try? JSONDecoder().decode([WatchSnapshot].self, from: data) {
            records = saved
        }
    }

    func add(_ snapshot: WatchSnapshot) {
        if records.last?.id == snapshot.id { return }
        if !detailedRecording, let last = records.last, last.deviceID == snapshot.deviceID,
           snapshot.receivedAt.timeIntervalSince(last.receivedAt) < 60,
           Calendar.current.isDate(snapshot.receivedAt, inSameDayAs: last.receivedAt) { return }
        records.append(snapshot)
        if records.count > 10_000 { records.removeFirst(records.count - 10_000) }
        scheduleSave()
    }

    /// Snapshots arrive twice per second; encoding up to 10,000 records on each
    /// one would stall the main actor. Coalesce writes to at most one per 5 s.
    private var saveScheduled = false
    private func scheduleSave() {
        if saveScheduled { return }
        saveScheduled = true
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: 5_000_000_000)
            saveNow()
        }
    }
    func saveNow() {
        saveScheduled = false
        if let data = try? JSONEncoder().encode(records) { try? data.write(to: fileURL, options: .atomic) }
    }

    /// Steps credited today. Stored records are at most one per minute unless
    /// detailed recording is on, so pass the latest live snapshot to extend the
    /// total up to the most recent BLE packet; it is counted the same way a
    /// stored record would be and is never double counted once stored.
    func dailySteps(on day: Date = Date(), live: WatchSnapshot? = nil) -> UInt32 {
        var result: UInt32 = 0
        var previousByDevice: [String: WatchSnapshot] = [:]
        var sequence = records
        if let live, records.last?.id != live.id { sequence.append(live) }
        for record in sequence {
            if let previous = previousByDevice[record.deviceID],
               record.uptimeMs >= previous.uptimeMs, record.steps >= previous.steps,
               Calendar.current.isDate(record.receivedAt, inSameDayAs: day) {
                result &+= record.steps - previous.steps
            }
            previousByDevice[record.deviceID] = record
        }
        return result
    }

    func exportCSV() -> URL? {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("healthwatch.csv")
        var lines = ["received_at,device_id,sequence,uptime_ms,steps,cadence_spm,heart_rate_bpm,spo2_percent,activity,imu_ready,ppg_ready,contact_detected,ppg_quality,possible_fall"]
        let iso = ISO8601DateFormatter()
        for r in records {
            lines.append("\(iso.string(from: r.receivedAt)),\(r.deviceID),\(r.sequence),\(r.uptimeMs),\(r.steps),\(r.cadence),\(r.heartRate.map { String($0) } ?? ""),\(r.oxygen.map { String($0) } ?? ""),\(r.activity.title),\(r.imuReady),\(r.ppgReady),\(r.contactGood),\(r.ppgQuality.title),\(r.possibleFall)")
        }
        do { try lines.joined(separator: "\n").write(to: url, atomically: true, encoding: .utf8); return url }
        catch { return nil }
    }
}
