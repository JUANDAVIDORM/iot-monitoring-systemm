import javax.swing.*;
import javax.swing.table.DefaultTableModel;
import java.awt.*;
import java.awt.event.ActionEvent;
import java.io.*;
import java.net.Socket;

public class OperatorClient extends JFrame {
    private JTextField hostField;
    private JTextField portField;
    private JTextField userField;
    private JPasswordField passField;
    private JButton connectButton;
    private JButton subscribeButton;
    private JButton refreshSensorsButton;
    private JTextArea alertsArea;
    private JTable sensorsTable;

    private Socket socket;
    private BufferedReader reader;
    private BufferedWriter writer;

    private Thread alertsThread;

    public OperatorClient() {
        super("IoT Monitoring Operator Client");
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setSize(800, 600);
        setLocationRelativeTo(null);
        initUI();
    }

    private void initUI() {
        JPanel topPanel = new JPanel(new GridLayout(2, 1));

        JPanel connectionPanel = new JPanel(new FlowLayout(FlowLayout.LEFT));
        hostField = new JTextField("iot-monitoring.example.com", 20);
        portField = new JTextField("5000", 6);
        connectionPanel.add(new JLabel("Host:"));
        connectionPanel.add(hostField);
        connectionPanel.add(new JLabel("Puerto:"));
        connectionPanel.add(portField);

        JPanel authPanel = new JPanel(new FlowLayout(FlowLayout.LEFT));
        userField = new JTextField("operador1", 10);
        passField = new JPasswordField("clave1", 10);
        connectButton = new JButton("Conectar");
        connectButton.addActionListener(this::onConnect);
        authPanel.add(new JLabel("Usuario:"));
        authPanel.add(userField);
        authPanel.add(new JLabel("Clave:"));
        authPanel.add(passField);
        authPanel.add(connectButton);

        topPanel.add(connectionPanel);
        topPanel.add(authPanel);

        add(topPanel, BorderLayout.NORTH);

        DefaultTableModel model = new DefaultTableModel(new Object[]{"ID", "Tipo", "Ultimo valor", "Ultima medicion"}, 0);
        sensorsTable = new JTable(model);
        JScrollPane sensorsScroll = new JScrollPane(sensorsTable);

        alertsArea = new JTextArea();
        alertsArea.setEditable(false);
        JScrollPane alertsScroll = new JScrollPane(alertsArea);

        JSplitPane splitPane = new JSplitPane(JSplitPane.VERTICAL_SPLIT, sensorsScroll, alertsScroll);
        splitPane.setResizeWeight(0.5);
        add(splitPane, BorderLayout.CENTER);

        JPanel bottomPanel = new JPanel(new FlowLayout(FlowLayout.LEFT));
        subscribeButton = new JButton("Suscribirse a alertas");
        subscribeButton.addActionListener(this::onSubscribeAlerts);
        subscribeButton.setEnabled(false);
        refreshSensorsButton = new JButton("Actualizar sensores");
        refreshSensorsButton.addActionListener(this::onRefreshSensors);
        refreshSensorsButton.setEnabled(false);
        bottomPanel.add(subscribeButton);
        bottomPanel.add(refreshSensorsButton);
        add(bottomPanel, BorderLayout.SOUTH);
    }

    private void onConnect(ActionEvent e) {
        if (socket != null && socket.isConnected()) {
            JOptionPane.showMessageDialog(this, "Ya conectado", "Info", JOptionPane.INFORMATION_MESSAGE);
            return;
        }
        String host = hostField.getText().trim();
        int port;
        try {
            port = Integer.parseInt(portField.getText().trim());
        } catch (NumberFormatException ex) {
            JOptionPane.showMessageDialog(this, "Puerto invalido", "Error", JOptionPane.ERROR_MESSAGE);
            return;
        }
        String user = userField.getText().trim();
        String pass = new String(passField.getPassword());

        try {
            socket = new Socket(host, port);
            reader = new BufferedReader(new InputStreamReader(socket.getInputStream()));
            writer = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream()));

            sendLine("HELLO OPERATOR");
            String resp = reader.readLine();
            if (!"AUTH REQUIRED".equals(resp)) {
                JOptionPane.showMessageDialog(this, "Servidor no responde AUTH REQUIRED", "Error", JOptionPane.ERROR_MESSAGE);
                return;
            }
            sendLine("LOGIN " + user + " " + pass);
            resp = reader.readLine();
            if (!"OK LOGIN".equals(resp)) {
                JOptionPane.showMessageDialog(this, "Autenticacion fallida: " + resp, "Error", JOptionPane.ERROR_MESSAGE);
                return;
            }

            subscribeButton.setEnabled(true);
            refreshSensorsButton.setEnabled(true);

            alertsThread = new Thread(this::listenMessages, "server-listener");
            alertsThread.setDaemon(true);
            alertsThread.start();

            JOptionPane.showMessageDialog(this, "Conectado y autenticado", "OK", JOptionPane.INFORMATION_MESSAGE);
        } catch (IOException ex) {
            JOptionPane.showMessageDialog(this, "Error de conexion: " + ex.getMessage(), "Error", JOptionPane.ERROR_MESSAGE);
        }
    }

    private synchronized void sendLine(String line) throws IOException {
        writer.write(line + "\n");
        writer.flush();
    }

    private void onSubscribeAlerts(ActionEvent e) {
        try {
            sendLine("SUBSCRIBE ALERTS");
            alertsArea.append("Enviada solicitud de suscripcion a alertas...\n");
        } catch (IOException ex) {
            alertsArea.append("Error al suscribirse: " + ex.getMessage() + "\n");
        }
    }

    private void onRefreshSensors(ActionEvent e) {
        try {
            sendLine("GET SENSORS");
            alertsArea.append("Solicitando lista de sensores al servidor...\n");
        } catch (IOException ex) {
            alertsArea.append("Error al solicitar sensores: " + ex.getMessage() + "\n");
        }
    }

    private void listenMessages() {
        try {
            String line;
            boolean readingSensors = false;
            DefaultTableModel model = (DefaultTableModel) sensorsTable.getModel();
            while ((line = reader.readLine()) != null) {
                final String msg = line;
                if (msg.startsWith("ALERT ")) {
                    SwingUtilities.invokeLater(() -> alertsArea.append(msg + "\n"));
                } else if (msg.startsWith("SENSOR ")) {
                    String[] parts = msg.split(" ");
                    if (parts.length >= 5) {
                        String id = parts[1];
                        String type = parts[2];
                        String value = parts[3];
                        String ts = parts[4];
                        if (!readingSensors) {
                            readingSensors = true;
                            SwingUtilities.invokeLater(() -> model.setRowCount(0));
                        }
                        SwingUtilities.invokeLater(() -> model.addRow(new Object[]{id, type, value, ts}));
                    }
                } else if ("END".equals(msg)) {
                    readingSensors = false;
                } else {
                    SwingUtilities.invokeLater(() -> alertsArea.append("Servidor: " + msg + "\n"));
                }
            }
        } catch (IOException ex) {
            SwingUtilities.invokeLater(() -> alertsArea.append("Conexion cerrada: " + ex.getMessage() + "\n"));
        }
    }

    public static void main(String[] args) {
        SwingUtilities.invokeLater(() -> {
            OperatorClient client = new OperatorClient();
            client.setVisible(true);
        });
    }
}
