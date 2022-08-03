#include <stdio.h>
#include <string.h>
#include <omnetpp.h>

using namespace omnetpp;

/**
 * Point-to-point interface module. While one frame is transmitted,
 * additional frames get queued up; see NED file for more info.
 */
class L2Queue : public cSimpleModule
{
  private:
    long frameCapacity;

    cQueue queue;
    cMessage *endTransmissionEvent;
    bool isBusy;

    simsignal_t qlenSignal;
    simsignal_t busySignal;
    simsignal_t queueingTimeSignal;
    simsignal_t dropSignal;
    simsignal_t txBytesSignal;
    simsignal_t rxBytesSignal;

  public:
    L2Queue();
    virtual ~L2Queue();

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void refreshDisplay() const override;
    virtual void startTransmitting(cMessage *msg);
};

Define_Module(L2Queue);

L2Queue::L2Queue()
{
    endTransmissionEvent = nullptr;
}

L2Queue::~L2Queue()
{
    cancelAndDelete(endTransmissionEvent);
}
//�ó��򴴽���һ����Ϣqueue���������˲�ͬ��signal
void L2Queue::initialize()
{
    queue.setName("queue");
    endTransmissionEvent = new cMessage("endTxEvent");

    if (par("useCutThroughSwitching"))
      // �˷���ֻ���ڼ�ģ����������ϵ��á����Ȳ�Ϊ�����Ϣ
      //����з��㴫�����ʱ�䣨��ˣ�������һ�˵Ľ��ճ���ʱ�䣩��
      //Ĭ������£�����Ϣ���ݵ�ģ���־�Ž��յĽ��������ô�λ����
      //��ͨ���ڽ��տ�ʼʱ����Ϣ���ݵ�ģ�顣���ս���ȡ�ĳ���ʱ���
      //��ͨ���� getDuration���� ��������Ϣ��������ȡ��
        gate("line$i")->setDeliverOnReceptionStart(true);//��˼���ǽ������ڽ��յ���Ϣʱ���̷���һ����Ϣ�������ǵȴ���Ϣȫ��������ϲŷ�����Ϣ

    frameCapacity = par("frameCapacity");

    qlenSignal = registerSignal("qlen");//registerSignal�����������ź�������Ϊ��������������Ӧ��simsignal_tֵ��
    busySignal = registerSignal("busy");
    queueingTimeSignal = registerSignal("queueingTime");
    dropSignal = registerSignal("drop");
    txBytesSignal = registerSignal("txBytes");
    rxBytesSignal = registerSignal("rxBytes");

    emit(qlenSignal, queue.getLength());
    emit(busySignal, false);
    isBusy = false;
}

void L2Queue::startTransmitting(cMessage *msg)
{
    EV << "Starting transmission of " << msg << endl;
    isBusy = true;
    int64_t numBytes = check_and_cast<cPacket *>(msg)->getByteLength();//check_and_cast<A *>:ת��ΪA���͵�ָ�룬ʧ���򷵻�NULL
    send(msg, "line$o");

    emit(txBytesSignal, (long)numBytes);

    // Schedule an event for the time when last bit will leave the gate.
    simtime_t endTransmission = gate("line$o")->getTransmissionChannel()->getTransmissionFinishTime();
    scheduleAt(endTransmission, endTransmissionEvent);//���������ʱ�Լ����Լ�����һ����Ϣ
}

void L2Queue::handleMessage(cMessage *msg)
{
    if (msg == endTransmissionEvent) {//���1���յ����Լ����͸��Լ�����Ϣ������ϢΪline88���͵ġ�
                                      //�յ�����Ϣ��ʼ����queue����һ����Ϣ��
        // Transmission finished, we can start next one.
        EV << "Transmission finished.\n";
        isBusy = false;
        if (queue.isEmpty()) {//isEmpty():���Ϊ�շ���true�������������false
            emit(busySignal, false);
        }
        else {
            msg = (cMessage *)queue.pop();//pop()���Է��ر�ɾ����Ԫ��
            emit(queueingTimeSignal, simTime() - msg->getTimestamp());
            emit(qlenSignal, queue.getLength());
            startTransmitting(msg);
        }
    }
    else if (msg->arrivedOn("line$i") || msg->arrivedOn("direct")) {//���2��������ת������ģ�鷢������Ϣ
        // pass up
        emit(rxBytesSignal, (long)check_and_cast<cPacket *>(msg)->getByteLength());
        send(msg, "out");
    }
    else {  // arrived on gate "in"//���3���յ���Ϣ
        //����endTransmissionEvent�ڰ����У��ڼƻ��У����ж϶����Ƿ��������������������������ջ
        if (endTransmissionEvent->isScheduled()) {//The isScheduled() method returns true if the message is currently scheduled.
            // We are currently busy, so just queue up the packet.
            if (frameCapacity && queue.getLength() >= frameCapacity) {
                EV << "Received " << msg << " but transmitter busy and queue full: discarding\n";
                emit(dropSignal, (long)check_and_cast<cPacket *>(msg)->getByteLength());
                delete msg;
            }
            else {
                EV << "Received " << msg << " but transmitter busy: queueing up\n";
                msg->setTimestamp();
                queue.insert(msg);
                emit(qlenSignal, queue.getLength());
            }
        }
        //��û��endTransmissionEvent�ڰ����У��ڼƻ��У�����ֱ�ӽ��ܸ���Ϣ����ʼ���ͣ�����Ҫ��ջ
        else {
            // We are idle, so we can start transmitting right away.
            EV << "Received " << msg << endl;
            emit(queueingTimeSignal, SIMTIME_ZERO);
            startTransmitting(msg);
            emit(busySignal, true);
        }
    }
}

void L2Queue::refreshDisplay() const
{
    getDisplayString().setTagArg("t", 0, isBusy ? "transmitting" : "idle");
    getDisplayString().setTagArg("i", 1, isBusy ? (queue.getLength() >= 3 ? "red" : "yellow") : "");
}

