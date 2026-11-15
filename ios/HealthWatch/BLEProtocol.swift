import Foundation

enum WatchProtocol {
    static let service = UUID(uuidString: "f0a1c001-0cb6-4f90-9c6c-4af42ca31f01")!
    static let snapshot = UUID(uuidString: "f0a1c002-0cb6-4f90-9c6c-4af42ca31f01")!
}

enum Activity: UInt8, Codable {
    case unknown = 0, stationary, walking, running, generalMovement
    var title: String {
        switch self {
        case .unknown: return "Unknown"
        case .stationary: return "Stationary"
        case .walking: return "Walking"
        case .running: return "Running"
        case .generalMovement: return "General movement"
        }
    }
}

enum PPGQuality: UInt8, Codable {
    case noContact = 0, invalid, degraded, good
    var title: String {
        switch self {
        case .noContact: return "No contact"
        case .invalid: return "Invalid"
        case .degraded: return "Degraded"
        case .good: return "Good"
        }
    }
}

struct WatchSnapshot: Codable, Identifiable {
    var id: String { "\(deviceID)-\(uptimeMs)-\(sequence)" }
    let deviceID: String
    let receivedAt: Date
    let sequence: UInt32
    let uptimeMs: UInt32
    let steps: UInt32
    let cadence: UInt16
    let heartRate: UInt8?
    let oxygen: UInt8?
    let imuReady: Bool
    let ppgReady: Bool
    let contactGood: Bool
    let possibleFall: Bool
    let ppgQuality: PPGQuality
    let activity: Activity

    static func decode(_ data: Data, deviceID: String, at date: Date = Date()) -> Self? {
        let b = [UInt8](data)
        guard b.count == 20, b[0] == 1, let activity = Activity(rawValue: b[2]),
              let quality = PPGQuality(rawValue: b[3]) else { return nil }
        func u16(_ i: Int) -> UInt16 { UInt16(b[i]) | UInt16(b[i+1]) << 8 }
        func u32(_ i: Int) -> UInt32 {
            UInt32(b[i]) | UInt32(b[i+1]) << 8 | UInt32(b[i+2]) << 16 | UInt32(b[i+3]) << 24
        }
        let flags = b[1]
        return Self(deviceID: deviceID, receivedAt: date, sequence: u32(4), uptimeMs: u32(8),
                    steps: u32(12), cadence: u16(16), heartRate: flags & 4 != 0 ? b[18] : nil,
                    oxygen: flags & 16 != 0 ? b[19] : nil, imuReady: flags & 1 != 0,
                    ppgReady: flags & 2 != 0, contactGood: flags & 8 != 0,
                    possibleFall: flags & 32 != 0,
                    ppgQuality: quality, activity: activity)
    }
}
