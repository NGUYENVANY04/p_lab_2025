from fastapi import HTTPException
from sqlalchemy.orm import Session
from models import AccountModel
from dotenv import load_dotenv
from datetime import datetime
import os, requests, asyncio, json

load_dotenv('config.env')
latest_transaction = None
polling_active = False


PERSISTENT_FILE = "last_transaction.json"


SEPAY_API_URL = os.getenv('SEPAY_API_URL')
API_TOKEN = os.getenv('API_TOKEN')

def save_data(data):
    try:
        if os.path.exists(PERSISTENT_FILE):
            os.remove(PERSISTENT_FILE)
        with open(PERSISTENT_FILE, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        return HTTPException(status_code=200, detail="Data saved successfully")
    except Exception as e:
        return HTTPException(status_code=500, detail=f"Error saving data: {str(e)}")


def load_data():
    try:
        if os.path.exists(PERSISTENT_FILE):
            with open(PERSISTENT_FILE, 'r', encoding='utf-8') as f:
                data = json.load(f)
            return data
        else:
            return None
    except Exception :
        return None 

def check_account(transaction: dict):
    id_student = transaction[0]["transaction_content"]
    return id_student

def update_balance(db: Session, id_student: str, amount: str):
    account = db.query(AccountModel).filter(AccountModel.id_student == id_student).first()
    if account:
        account.so_du += float(amount)
        db.commit()
        print("Balance updated successfully")
        return {"message": "Balance updated successfully"}
    else:
        return {"message": "Account not found"}

def create_account(db: Session, id_card: str, password: str):
    # Kiểm tra thẻ đã tồn tại chưa
    existing_account = db.query(AccountModel).filter(
        AccountModel.id_card == id_card
    ).first()
    
    if existing_account:
        # Trả về lỗi 409 Conflict
        raise HTTPException(
            status_code=409,
            detail={
                "error": "Card already exists",
                "message": "Thẻ đã tồn tại trong hệ thống",
                "card_id": id_card
            }
        )
    
    # Tạo tài khoản mới
    db_account = AccountModel(id_card=id_card, password=password)
    db.add(db_account)
    db.commit()
    db.refresh(db_account)
    return db_account


def delete_account(db: Session, id_student: str):
    account = db.query(AccountModel).filter(AccountModel.id_student == id_student).first()
    if account:
        db.delete(account)
        db.commit()
        return {"message": "Account deleted successfully"}
    else:
        return {"message": "Account not found"}
    
async def poll_transactions(db: Session):
    global latest_transaction, polling_active
    last_transaction = load_data()
    last_id=last_transaction[0]["id"]
    print("latest_transaction",last_id)
    while polling_active:
        try:
            url = f"{SEPAY_API_URL}?since_id={last_id}&limit=1"
            headers = {
                "Authorization": f"Bearer {API_TOKEN}",
                "Content-Type": "application/json"
            }
            response = requests.get(url, headers=headers)
            if response.status_code == 200:
                data = response.json()
                new_transaction = data["transactions"]
                new_id=new_transaction[0]["id"]
                last_transaction = new_transaction
                save_data(last_transaction)
                id_student = check_account(new_transaction).split(" ")[0]
                if new_id==last_id:
                    print("No new transaction.")
                    await asyncio.sleep(3)
                    continue
                account_exist=db.query(AccountModel).filter(AccountModel.id_student==id_student).first()
                if account_exist:
                    update_balance(db, id_student, new_transaction[0]["amount_in"])
                    last_id=new_id
            elif response.status_code == 429:
                print(f"[{datetime.now()}] Rate limit exceeded (429)")
                await asyncio.sleep(5)  
                continue
            else:
                print(f"[{datetime.now()}]Error: {response.status_code} - {response.text}")
        except Exception as e:
            print(f"[{datetime.now()}] Exception: {str(e)}")
        await asyncio.sleep(2)